#include "ccache.h"

#ifdef HAVE_LIBMEMCACHED

#include <libmemcached/memcached.h>
#include <netinet/in.h>

#define MEMCCACHE_MAGIC "CCH1"
#define MEMCCACHE_BIG "CCBM"

#define MAX_VALUE_SIZE (1000 << 10) /* 1M with memcached overhead */
#define SPLIT_VALUE_SIZE MAX_VALUE_SIZE

/* status variables for memcached */
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
	/* Consistent hashing delivers better distribution and allows servers to be
	   added to the cluster with minimal cache losses */
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_DISTRIBUTION,
	                       MEMCACHED_DISTRIBUTION_CONSISTENT);
	return 0;
}

/* blob format for big values:

    char magic[4]; # 'CCBM'
    uint32_t numkeys; # network endian
    uint32_t hash_size; # network endian
    uint32_t reserved; # network endian
    uint32_t value_length; # network endian

    <hash[0]>       hash of include file                (<hash_size> bytes)
    <size[0]>       size of include file                (4 bytes unsigned int)
    ...
    <hash[n-1]>
    <size[n-1]>

 */
static memcached_return_t memccached_big_set(memcached_st *ptr,
                                             const char *key,
                                             size_t key_length,
                                             const char *value,
                                             size_t value_length,
                                             time_t expiration,
                                             uint32_t flags)
{
	char *buf;
	size_t buflen;
	char *p;
	int numkeys;
	struct mdfour md;
	char subkey[20];
	size_t n;
	memcached_return_t ret;
	size_t x;

	numkeys = (value_length + SPLIT_VALUE_SIZE - 1) / SPLIT_VALUE_SIZE;
	buflen = 20 + 20 * numkeys;
	buf = x_malloc(buflen);
	p = buf;

	memcpy(p, MEMCCACHE_BIG, 4);
	*((uint32_t *) (p + 4)) = htonl(numkeys);
	*((uint32_t *) (p + 8)) = htonl(16);
	*((uint32_t *) (p + 12)) = htonl(0);
	*((uint32_t *) (p + 16)) = htonl(value_length);
	p += 20;

	for (x = 0; x < value_length; x += n) {
		size_t remain;
		char *s;

		remain = value_length - x;
		n = remain > SPLIT_VALUE_SIZE ? SPLIT_VALUE_SIZE : remain;

		mdfour_begin(&md);
		mdfour_update(&md, (const unsigned char *) value + x, n);
		mdfour_result(&md, (unsigned char *) subkey);
		*((uint32_t *) (subkey + 16)) = htonl(n);
		s = format_hash_as_string((const unsigned char *) subkey, n);
		cc_log("memcached_mset %s %ld", s, n);
		ret = memcached_set(ptr, s, strlen(s), value + x, n,
		                    expiration, flags);
		free(s);
		if (ret) {
			cc_log("Failed to set key in memcached: %s",
			       memcached_strerror(memc, ret));
			return ret;
		}

		memcpy(p, subkey, 20);
		p += 20;
	}

	cc_log("memcached_set %.*s %ld (%ld)", (int) key_length, key, buflen,
	       value_length);
	ret = memcached_set(ptr, key, key_length, buf, buflen,
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
	char *buf;
	size_t buflen;
	size_t totalsize;
	char *p;
	int numkeys;
	char **keys;
	size_t *key_lengths;
	size_t *key_offsets;
	memcached_return_t ret;
	memcached_result_st *result;
	int n;
	int i;

	if (!value) {
		value = memcached_get(ptr, key, key_length, value_length, flags, error);
		if (!value) {
			return NULL;
		}
	}

	p = (char *) value;
	if (memcmp(p, MEMCCACHE_BIG, 4) != 0) {
		return NULL;
	}
	numkeys = ntohl(*(uint32_t *) (p + 4));
	assert(ntohl(*(uint32_t *) (p + 8)) == 16);
	assert(ntohl(*(uint32_t *) (p + 12)) == 0);
	totalsize = ntohl(*(uint32_t *) (p + 16));
	p += 20;

	keys = x_malloc(sizeof(char *) * numkeys);
	key_lengths = x_malloc(sizeof(size_t) * numkeys);
	key_offsets = x_malloc(sizeof(size_t) * numkeys);

	buflen = 0;
	for (i = 0; i < numkeys; i++) {
		n = ntohl(*((uint32_t *) (p + 16)));
		keys[i] = format_hash_as_string((const unsigned char *) p, n);
		key_lengths[i] = strlen(keys[i]);
		cc_log("memcached_mget %.*s %d", (int) key_lengths[i], keys[i], n);
		key_offsets[i] = buflen;
		buflen += n;
		p += 20;
	}
	assert(buflen == totalsize);

	buf = x_malloc(buflen);

	ret = memcached_mget(ptr, (const char *const *) keys, key_lengths, numkeys);
	if (ret) {
		cc_log("Failed to mget keys in memcached: %s",
		       memcached_strerror(memc, ret));
		for (i = 0; i < numkeys; i++) {
			free(keys[i]);
		}
		free(keys);
		free(key_lengths);
		return NULL;
	}

	result = NULL;
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
		for (i = 0; i < numkeys; i++) {
			if (l != key_lengths[i]) {
				continue;
			}
			if (str_eq(k, keys[i])) {
				p = buf + key_offsets[i];
				break;
			}
		}
		if (!p) {
			cc_log("Unknown key was returned: %s", k);
			return NULL;
		}
		n = memcached_result_length(result);
		memcpy(p, memcached_result_value(result), n);
	} while (ret == MEMCACHED_SUCCESS);

	cc_log("memcached_get %.*s %ld (%ld)", (int) key_length, key, *value_length,
	       buflen);
	for (i = 0; i < numkeys; i++) {
		free(keys[i]);
	}
	free(keys);
	free(key_lengths);
	free(key_offsets);

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

/* blob format for storing:

    char magic[4]; # 'CCH1', might change for other version of ccache
 # ccache will erase the blob in memcached if wrong magic
    uint32_t obj_len; # network endian
    char *obj[obj_len];
    uint32_t stderr_len; # network endian
    char *stderr[stderr_len];
    uint32_t dia_len; # network endian
    char *dia[dia_len];
    uint32_t dep_len; # network endian
    char *dep[dep_len];

 */
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
	char *ptr;
	memcached_return_t mret;

	memcpy(buf, MEMCCACHE_MAGIC, 4);
	ptr = buf + 4;

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

	if (buf_len > MAX_VALUE_SIZE)
		mret = memccached_big_set(memc, key, strlen(key), buf, buf_len, 0, 0);
	else
		mret = memcached_set(memc, key, strlen(key), buf, buf_len, 0, 0);

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
	/* don't really care whether delete failed */
	memcached_delete(memc, key, strlen(key), 0);
	return NULL;
}

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
	return value;   /* caller must free this when done with the ptr */
}

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
	memcached_return_t mret;
	char *value, *ptr;
	size_t value_l;
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
		cc_log("wrong magic or length %s: %d", value, (int)value_l);
		free(value);
		return memccached_prune(key);
	}
	ptr = value;
	/* skip the magic */
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

	return value;  /* caller must free this when done with the ptrs */
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

#endif /* HAVE_LIBMEMCACHED */
