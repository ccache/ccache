// ccache -- a fast C/C++ compiler cache
//
// Copyright (C) 2016 Anders Björklund
// Copyright (C) 2016 Joel Rosdahl
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "ccache.h"

#ifdef HAVE_LIBMEMCACHED

#include <libmemcached/memcached.h>
#include <netinet/in.h>

#define MEMCCACHE_MAGIC "CCH1"
#define MEMCCACHE_BIG "CCBM"

#define MAX_VALUE_SIZE (1000 << 10) // ~1MB with memcached overhead
#define SPLIT_VALUE_SIZE MAX_VALUE_SIZE

// Status variables for memcached.
static memcached_st *memc;

int memccached_init(char *conf)
{
	memc = memcached(conf, strlen(conf));
	if (!memc) {
		char errorbuf[1024];
		libmemcached_check_configuration(conf, strlen(conf), errorbuf, 1024);
		cc_log("Problem creating memcached with conf %s:\n%s\n", conf, errorbuf);
		return -1;
	}
	// Consistent hashing delivers better distribution and allows servers to be
	// added to the cluster with minimal cache losses.
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_DISTRIBUTION,
	                       MEMCACHED_DISTRIBUTION_CONSISTENT);
	return 0;
}

// Blob format for big values:
//
//   char magic[4]; # 'CCBM'
//   uint32_t numkeys; # network endian
//   uint32_t hash_size; # network endian
//   uint32_t reserved; # network endian
//   uint32_t value_length; # network endian
//
//   <hash[0]>       hash of include file                (<hash_size> bytes)
//   <size[0]>       size of include file                (4 bytes unsigned int)
//   ...
//   <hash[n-1]>
//   <size[n-1]>

static memcached_return_t memccached_big_set(memcached_st *ptr,
                                             const char *key,
                                             size_t key_length,
                                             const char *value,
                                             size_t value_length,
                                             time_t expiration,
                                             uint32_t flags)
{
	int numkeys = (value_length + SPLIT_VALUE_SIZE - 1) / SPLIT_VALUE_SIZE;
	size_t buflen = 20 + 20 * numkeys;
	char *buf = x_malloc(buflen);
	char *p = buf;

	memcpy(p, MEMCCACHE_BIG, 4);
	*((uint32_t *) (p + 4)) = htonl(numkeys);
	*((uint32_t *) (p + 8)) = htonl(16);
	*((uint32_t *) (p + 12)) = htonl(0);
	*((uint32_t *) (p + 16)) = htonl(value_length);
	p += 20;

	size_t n = 0;
	for (size_t x = 0; x < value_length; x += n) {
		size_t remain = value_length - x;
		n = remain > SPLIT_VALUE_SIZE ? SPLIT_VALUE_SIZE : remain;

		struct mdfour md;
		mdfour_begin(&md);
		mdfour_update(&md, (const unsigned char *) value + x, n);

		char subkey[20];
		mdfour_result(&md, (unsigned char *) subkey);
		*((uint32_t *) (subkey + 16)) = htonl(n);

		char *s = format_hash_as_string((const unsigned char *) subkey, n);
		cc_log("memcached_mset %s %zu", s, n);
		memcached_return_t ret = memcached_set(
			ptr, s, strlen(s), value + x, n, expiration, flags);
		free(s);
		if (ret) {
			cc_log("Failed to set key in memcached: %s",
			       memcached_strerror(memc, ret));
			return ret;
		}

		memcpy(p, subkey, 20);
		p += 20;
	}

	cc_log("memcached_set %.*s %zu (%zu)", (int) key_length, key, buflen,
	       value_length);
	int ret = memcached_set(ptr, key, key_length, buf, buflen,
	                        expiration, flags);
	free(buf);
	return ret;
}

static char *memccached_big_get(memcached_st *ptr,
                                const char *key,
                                size_t key_length,
                                const char *value,
                                size_t *value_length,
                                uint32_t *flags,
                                memcached_return_t *error)
{
	if (!value) {
		value = memcached_get(ptr, key, key_length, value_length, flags, error);
		if (!value) {
			return NULL;
		}
	}

	char *p = (char *)value;
	if (memcmp(p, MEMCCACHE_BIG, 4) != 0) {
		return NULL;
	}

	int numkeys = ntohl(*(uint32_t *) (p + 4));
	assert(ntohl(*(uint32_t *) (p + 8)) == 16);
	assert(ntohl(*(uint32_t *) (p + 12)) == 0);
	size_t totalsize = ntohl(*(uint32_t *) (p + 16));
	p += 20;

	char **keys = x_malloc(sizeof(char *) * numkeys);
	bool *key_seen = x_malloc(sizeof(bool) * numkeys);
	size_t *key_lengths = x_malloc(sizeof(size_t) * numkeys);
	size_t *value_offsets = x_malloc(sizeof(size_t) * numkeys);
	int *value_lengths = x_malloc(sizeof(int) * numkeys);

	size_t buflen = 0;
	for (int i = 0; i < numkeys; i++) {
		int n = ntohl(*((uint32_t *) (p + 16)));
		keys[i] = format_hash_as_string((const unsigned char *) p, n);
		key_lengths[i] = strlen(keys[i]);
		key_seen[i] = false;
		cc_log("memcached_mget %.*s %d", (int) key_lengths[i], keys[i], n);
		value_offsets[i] = buflen;
		value_lengths[i] = n;
		buflen += n;
		p += 20;
	}
	assert(buflen == totalsize);

	char *buf = x_malloc(buflen);

	memcached_return_t	ret = memcached_mget(
		ptr, (const char *const *) keys, key_lengths, numkeys);
	if (ret) {
		cc_log("Failed to mget keys in memcached: %s",
		       memcached_strerror(memc, ret));
		for (int i = 0; i < numkeys; i++) {
			free(keys[i]);
		}
		free(keys);
		free(key_lengths);
		return NULL;
	}

	memcached_result_st *result = NULL;
	do {
		const char *k;
		size_t l;

		result = memcached_fetch_result(ptr, result, &ret);
		if (ret == MEMCACHED_END) {
			break;
		}
		if (ret) {
			cc_log("Failed to get key in memcached: %s",
			       memcached_strerror(memc, ret));
			return NULL;
		}
		k = memcached_result_key_value(result);
		l = memcached_result_key_length(result);
		p = NULL;
		int i;
		for (i = 0; i < numkeys; i++) {
			if (l != key_lengths[i]) {
				continue;
			}
			if (memcmp(k, keys[i], l) == 0) {
				p = buf + value_offsets[i];
				break;
			}
		}
		if (!p) {
			cc_log("Unknown key was returned: %s", k);
			return NULL;
		}
		if (key_seen[i]) {
			cc_log("Have already seen chunk: %s", k);
			return NULL;
		}
		key_seen[i] = true;
		int n = memcached_result_length(result);
		const char *v = memcached_result_value(result);
		if (n != value_lengths[i]) {
			cc_log("Unexpected length was returned");
			return NULL;
		}
		memcpy(p, v, n);
	} while (ret == MEMCACHED_SUCCESS);

	for (int i = 0; i < numkeys; i++) {
		if (!key_seen[i]) {
			cc_log("Failed to get all %d chunks", numkeys);
			return NULL;
		}
	}
	cc_log("memcached_get %.*s %zu (%zu)", (int) key_length, key, *value_length,
	       buflen);
	for (int i = 0; i < numkeys; i++) {
		free(keys[i]);
	}
	free(keys);
	free(key_lengths);
	free(value_offsets);
	free(value_lengths);

	*value_length = buflen;
	return buf;
}

int memccached_raw_set(const char *key, const char *data, size_t len)
{
	memcached_return_t mret;

	mret = memcached_set(memc, key, strlen(key), data, len, 0, 0);
	if (mret != MEMCACHED_SUCCESS) {
		cc_log("Failed to move %s to memcached: %s", key,
		       memcached_strerror(memc, mret));
		return -1;
	}
	return 0;
}

// Blob format for storing:

//   char magic[4]; # 'CCH1', might change for other version of ccache
// # ccache will erase the blob in memcached if wrong magic
//   uint32_t obj_len; # network endian
//   char *obj[obj_len];
//   uint32_t stderr_len; # network endian
//   char *stderr[stderr_len];
//   uint32_t dia_len; # network endian
//   char *dia[dia_len];
//   uint32_t dep_len; # network endian
//   char *dep[dep_len];

int memccached_set(const char *key,
                   const char *obj,
                   const char *stderr,
                   const char *dia,
                   const char *dep,
                   size_t obj_len,
                   size_t stderr_len,
                   size_t dia_len,
                   size_t dep_len)
{
	size_t buf_len = 4 + 4*4 + obj_len + stderr_len + dia_len + dep_len;
	char *buf = x_malloc(buf_len);
	memcpy(buf, MEMCCACHE_MAGIC, 4);
	char *ptr = buf + 4;

#define PROCESS_ONE_BUFFER(src_ptr, src_len) \
	do { \
		*((uint32_t *)ptr) = htonl(src_len); \
		ptr += 4; \
		if (src_len > 0) { \
			memcpy(ptr, src_ptr, src_len); \
		} \
		ptr += src_len; \
	} while (false)

	PROCESS_ONE_BUFFER(obj, obj_len);
	PROCESS_ONE_BUFFER(stderr, stderr_len);
	PROCESS_ONE_BUFFER(dia, dia_len);
	PROCESS_ONE_BUFFER(dep, dep_len);

#undef PROCESS_ONE_BUFFER

	memcached_return_t mret;
	if (buf_len > MAX_VALUE_SIZE) {
		mret = memccached_big_set(memc, key, strlen(key), buf, buf_len, 0, 0);
	} else {
		mret = memcached_set(memc, key, strlen(key), buf, buf_len, 0, 0);
	}

	if (mret != MEMCACHED_SUCCESS) {
		cc_log("Failed to move %s to memcached: %s", key,
		       memcached_strerror(memc, mret));
		return -1;
	}
	return 0;
}

static void *memccached_prune(const char *key)
{
	cc_log("key from memcached has wrong data %s: pruning...", key);
	// Don't really care whether delete failed.
	memcached_delete(memc, key, strlen(key), 0);
	return NULL;
}

// Caller should free the return calue when done with the pointers.
void *memccached_raw_get(const char *key, char **data, size_t *size)
{
	memcached_return_t mret;
	void *value;
	size_t value_l;

	value = memcached_get(memc, key, strlen(key), &value_l,
	                      NULL /*flags*/, &mret);
	if (!value) {
		cc_log("Failed to get key from memcached %s: %s", key,
		       memcached_strerror(memc, mret));
		return NULL;
	}
	*data = value;
	*size = value_l;
	return value;
}

// Caller should free the return value when done with the pointers.
void *memccached_get(const char *key,
                     char **obj,
                     char **stderr,
                     char **dia,
                     char **dep,
                     size_t *obj_len,
                     size_t *stderr_len,
                     size_t *dia_len,
                     size_t *dep_len)
{
	char *value;
	size_t value_l;
	memcached_return_t mret;
	value = memcached_get(memc, key, strlen(key), &value_l,
	                      NULL /*flags*/, &mret);
	if (!value) {
		cc_log("Failed to get key from memcached %s: %s", key,
		       memcached_strerror(memc, mret));
		return NULL;
	}
	if (value_l > 4 && memcmp(value, MEMCCACHE_BIG, 4) == 0) {
		value = memccached_big_get(memc, key, strlen(key), value, &value_l,
		                           NULL /*flags*/, &mret);
	}
	if (!value) {
		cc_log("Failed to get key from memcached %s: %s", key,
		       memcached_strerror(memc, mret));
		return NULL;
	}
	if (value_l < 20 || memcmp(value, MEMCCACHE_MAGIC, 4) != 0) {
		cc_log("wrong magic or length %.4s: %d", value, (int)value_l);
		free(value);
		return memccached_prune(key);
	}

	char *ptr = value;
	// Skip the magic.
	ptr += 4;
	value_l -= 4;

#define PROCESS_ONE_BUFFER(dst_ptr, dst_len) \
	do { \
		if (value_l < 4) { \
			free(value); \
			cc_log("no more buffer for %s: %d", \
			       #dst_ptr, (int)value_l); \
			return memccached_prune(key); \
		} \
		dst_len = ntohl(*((uint32_t *)ptr)); \
		ptr += 4; value_l -= 4; \
		if (value_l < dst_len) { \
			cc_log("no more buffer for %s: %d %d", \
			       #dst_ptr, (int)value_l, (int) dst_len); \
			free(value); \
			return memccached_prune(key); \
		} \
		dst_ptr = ptr; \
		ptr += dst_len; value_l -= dst_len; \
	} while (false)

	PROCESS_ONE_BUFFER(*obj, *obj_len);
	PROCESS_ONE_BUFFER(*stderr, *stderr_len);
	PROCESS_ONE_BUFFER(*dia, *dia_len);
	PROCESS_ONE_BUFFER(*dep, *dep_len);

#undef PROCESS_ONE_BUFFER

	return value;
}

void memccached_free(void *blob)
{
	free(blob);
}

int memccached_release(void)
{
	memcached_free(memc);
	return 1;
}

#endif // HAVE_LIBMEMCACHED
