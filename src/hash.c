// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2010-2018 Joel Rosdahl
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

#define HASH_DELIMITER "\000cCaChE"

struct hash {
	struct mdfour md;
	FILE *debug_binary;
	FILE *debug_text;
};

static void
do_hash_buffer(struct hash *hash, const void *s, size_t len)
{
	mdfour_update(&hash->md, (const unsigned char *)s, len);
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
	mdfour_begin(&hash->md);
	hash->debug_binary = NULL;
	hash->debug_text = NULL;
	return hash;
}

struct hash *
hash_copy(struct hash *hash)
{
	struct hash *result = malloc(sizeof(struct hash));
	result->md = hash->md;
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

size_t
hash_input_size(struct hash *hash)
{
	return hash->md.totalN;
}

void
hash_buffer(struct hash *hash, const void *s, size_t len)
{
	do_hash_buffer(hash, s, len);
	do_debug_text(hash, s, len);
}

char *
hash_result(struct hash *hash)
{
	unsigned char sum[16];

	hash_result_as_bytes(hash, sum);
	return format_hash_as_string(sum, (unsigned) hash->md.totalN);
}

void
hash_result_as_bytes(struct hash *hash, unsigned char *out)
{
	mdfour_update(&hash->md, NULL, 0);
	mdfour_result(&hash->md, out);
}

bool
hash_equal(struct hash *hash1, struct hash *hash2)
{
	unsigned char sum1[16];
	hash_result_as_bytes(hash1, sum1);
	unsigned char sum2[16];
	hash_result_as_bytes(hash2, sum2);
	return memcmp(sum1, sum2, sizeof(sum1)) == 0;
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
