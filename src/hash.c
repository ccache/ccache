// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2010-2016 Joel Rosdahl
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

#define HASH_DELIMITER "\000cCaChE"
#define HASH_DEBUG_DELIMITER "### "

// binary input, for hashing
char *debug_hash_bin;

// text input, for debugging
char *debug_hash_txt;

void hash_debug(const char *bin, const char *txt)
{
	static char *hash_types = "cdp"; // common, direct, cpp
	if (bin) {
		debug_hash_bin = x_strdup(bin);
		assert(debug_hash_bin[strlen(debug_hash_bin)-1] == 'X');
		for (char *p = hash_types; *p != '\0'; p++) {
			debug_hash_bin[strlen(debug_hash_bin)-1] = *p;
			x_try_unlink(debug_hash_bin);
		}
	}
	if (txt) {
		debug_hash_txt = x_strdup(txt);
		x_try_unlink(debug_hash_txt);
	}
}

static void
hash_binary_buffer(struct mdfour *md, const void *s, size_t len)
{
	mdfour_update(md, (unsigned char *)s, len);
	if (!md->identifier || len == 0) {
		return;
	}
	if (debug_hash_bin) {
		// log to different files, for the different hash types
		debug_hash_bin[strlen(debug_hash_bin)-1] = md->identifier;

		FILE *f = fopen(debug_hash_bin, "a");
		fwrite(s, 1, len, f);
		fclose(f);
	}
}

static void
hash_debug_buffer(struct mdfour *md, const void *s, size_t len)
{
	if (!md->identifier || len == 0) {
		return;
	}
	if (debug_hash_txt) {
		FILE *f = fopen(debug_hash_txt, "a");
		fwrite(s, 1, len, f);
		fclose(f);
	}
}

void
hash_start(struct mdfour *md)
{
	mdfour_begin(md);
}

void
hash_buffer(struct mdfour *md, const void *s, size_t len)
{
	hash_binary_buffer(md, s, len);
	hash_debug_buffer(md, s, len);
}

// Return the hash result as a hex string. Caller frees.
char *
hash_result(struct mdfour *md)
{
	unsigned char sum[16];

	hash_result_as_bytes(md, sum);
	return format_hash_as_string(sum, (unsigned) md->totalN);
}

// Return the hash result as 16 binary bytes.
void
hash_result_as_bytes(struct mdfour *md, unsigned char *out)
{
	mdfour_update(md, NULL, 0);
	mdfour_result(md, out);
}

bool
hash_equal(struct mdfour *md1, struct mdfour *md2)
{
	unsigned char sum1[16];
	hash_result_as_bytes(md1, sum1);
	unsigned char sum2[16];
	hash_result_as_bytes(md2, sum2);
	return memcmp(sum1, sum2, sizeof(sum1)) == 0;
}

// Hash some data that is unlikely to occur in the input. The idea is twofold:
//
// - Delimit things like arguments from each other (e.g., so that -I -O2 and
//   -I-O2 hash differently).
// - Tag different types of hashed information so that it's possible to do
//   conditional hashing of information in a safe way (e.g., if we want to hash
//   information X if CCACHE_A is set and information Y if CCACHE_B is set,
//   there should never be a hash collision risk).
void
hash_delimiter(struct mdfour *md, const char *type)
{
	hash_binary_buffer(md, HASH_DELIMITER, sizeof(HASH_DELIMITER));
	hash_binary_buffer(md, type, strlen(type) + 1); // Include NUL.
	hash_debug_buffer(md, HASH_DEBUG_DELIMITER, strlen(HASH_DEBUG_DELIMITER));
	hash_debug_buffer(md, type, strlen(type));
	hash_debug_buffer(md, "\n", 1);
}

void
hash_string(struct mdfour *md, const char *s)
{
	hash_string_length(md, s, strlen(s));
}

void
hash_string_length(struct mdfour *md, const char *s, int length)
{
	hash_binary_buffer(md, s, length);
	hash_debug_buffer(md, s, length);
	hash_debug_buffer(md, "\n", 1);
}

void
hash_int(struct mdfour *md, int x)
{
	hash_binary_buffer(md, (char *)&x, sizeof(x));
	char buf[16];
	snprintf(buf, sizeof(buf), "%d", x);
	hash_debug_buffer(md, buf, strlen(buf));
	hash_debug_buffer(md, "\n", 1);
}

// Add contents of an open file to the hash. Returns true on success, otherwise
// false.
bool
hash_fd(struct mdfour *md, int fd)
{
	char buf[READ_BUFFER_SIZE];
	ssize_t n;

	while ((n = read(fd, buf, sizeof(buf))) != 0) {
		if (n == -1 && errno != EINTR) {
			break;
		}
		if (n > 0) {
			hash_binary_buffer(md, buf, n);
			hash_debug_buffer(md, buf, n);
		}
	}
	return n == 0;
}

// Add contents of a file to the hash. Returns true on success, otherwise
// false.
bool
hash_file(struct mdfour *md, const char *fname)
{
	int fd = open(fname, O_RDONLY|O_BINARY);
	if (fd == -1) {
		cc_log("Failed to open %s: %s", fname, strerror(errno));
		return false;
	}

	bool ret = hash_fd(md, fd);
	close(fd);
	return ret;
}
