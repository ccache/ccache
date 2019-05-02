// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2010-2019 Joel Rosdahl
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
#include "hash.h"
#include "mdfour.h"

#ifdef HAVE_BLAKE2_H
#include <blake2.h>
#endif

#define HASH_DELIMITER "\000cCaChE"

struct hash {
#ifdef USE_BLAKE2
	blake2b_state state;
#else
	struct mdfour md;
#endif
	FILE *debug_binary;
	FILE *debug_text;
};

void
digest_as_string(const struct digest *d, char *buffer)
{
	format_hex(d->bytes, DIGEST_SIZE, buffer);
}

bool
digests_equal(const struct digest *d1, const struct digest *d2)
{
	return memcmp(d1->bytes, d2->bytes, DIGEST_SIZE) == 0;
}

static void
do_hash_buffer(struct hash *hash, const void *s, size_t len)
{
	assert(s);

#ifdef USE_BLAKE2
	blake2b_update(&hash->state, (const uint8_t *)s, len);
#else
	mdfour_update(&hash->md, (const unsigned char *)s, len);
#endif
	if (len > 0 && hash->debug_binary) {
		(void) fwrite(s, 1, len, hash->debug_binary);
	}
}

static void
do_debug_text(struct hash *hash, const void *s, size_t len)
{
	if (len > 0 && hash->debug_text) {
		(void) fwrite(s, 1, len, hash->debug_text);
	}
}

struct hash *
hash_init(void)
{
	struct hash *hash = malloc(sizeof(struct hash));
#ifdef USE_BLAKE2
	blake2b_init(&hash->state, DIGEST_SIZE);
#else
	mdfour_begin(&hash->md);
#endif
	hash->debug_binary = NULL;
	hash->debug_text = NULL;
	return hash;
}

struct hash *
hash_copy(struct hash *hash)
{
	struct hash *result = malloc(sizeof(struct hash));
#ifdef USE_BLAKE2
	result->state = hash->state;
#else
	result->md = hash->md;
#endif
	result->debug_binary = NULL;
	result->debug_text = NULL;
	return result;
}

void hash_free(struct hash *hash)
{
	free(hash);
}

void hash_enable_debug(
	struct hash *hash, const char *section_name,
	FILE *debug_binary, FILE *debug_text)
{
	hash->debug_binary = debug_binary;
	hash->debug_text = debug_text;

	do_debug_text(hash, "=== ", 4);
	do_debug_text(hash, section_name, strlen(section_name));
	do_debug_text(hash, " ===\n", 5);
}

void
hash_buffer(struct hash *hash, const void *s, size_t len)
{
	do_hash_buffer(hash, s, len);
	do_debug_text(hash, s, len);
}

void
hash_result_as_bytes(struct hash *hash, struct digest *digest)
{
#ifdef USE_BLAKE2
	// make a copy before altering state
	struct hash *copy = hash_copy(hash);
	blake2b_final(&copy->state, digest->bytes, DIGEST_SIZE);
	hash_free(copy);
#else
	mdfour_result(&hash->md, digest->bytes);
	size_t input_size = hash->md.totalN + hash->md.tail_len;
	for (size_t i = 0; i < 4; i++) {
		digest->bytes[16 + i] = (input_size >> ((3 - i) * 8)) & 0xFF;
	}
#endif
}

void
hash_result_as_string(struct hash *hash, char *buffer)
{
	struct digest d;
	hash_result_as_bytes(hash, &d);
	digest_as_string(&d, buffer);
}

void
hash_delimiter(struct hash *hash, const char *type)
{
	do_hash_buffer(hash, HASH_DELIMITER, sizeof(HASH_DELIMITER));
	do_hash_buffer(hash, type, strlen(type) + 1); // Include NUL.
	do_debug_text(hash, "### ", 4);
	do_debug_text(hash, type, strlen(type));
	do_debug_text(hash, "\n", 1);
}

void
hash_string(struct hash *hash, const char *s)
{
	hash_string_buffer(hash, s, strlen(s));
}

void
hash_string_buffer(struct hash *hash, const char *s, int length)
{
	hash_buffer(hash, s, length);
	do_debug_text(hash, "\n", 1);
}

void
hash_int(struct hash *hash, int x)
{
	do_hash_buffer(hash, (char *)&x, sizeof(x));

	char buf[16];
	snprintf(buf, sizeof(buf), "%d", x);
	do_debug_text(hash, buf, strlen(buf));
	do_debug_text(hash, "\n", 1);
}

bool
hash_fd(struct hash *hash, int fd)
{
	char buf[READ_BUFFER_SIZE];
	ssize_t n;

	while ((n = read(fd, buf, sizeof(buf))) != 0) {
		if (n == -1 && errno != EINTR) {
			break;
		}
		if (n > 0) {
			do_hash_buffer(hash, buf, n);
			do_debug_text(hash, buf, n);
		}
	}
	return n == 0;
}

bool
hash_file(struct hash *hash, const char *fname)
{
	int fd = open(fname, O_RDONLY|O_BINARY);
	if (fd == -1) {
		cc_log("Failed to open %s: %s", fname, strerror(errno));
		return false;
	}

	bool ret = hash_fd(hash, fd);
	close(fd);
	return ret;
}
