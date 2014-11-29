/*
 * Copyright (C) 2002 Andrew Tridgell
 * Copyright (C) 2010 Joel Rosdahl
 * Copyright (C) 2014 Timofey Titovets
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ccache.h"

#define HASH_DELIMITER "\000cCaChE"

unsigned hash_simple(const void* input, size_t length, unsigned seed) {
        return xxh32 (input, length, seed);
}

void
hash_start(HSTATE_T *hstate)
{
	xxh64_reset(hstate, 0);
}

size_t hash_get_len(HSTATE_T *hstate)
{
	return (size_t) hstate->total_len;
}

void
hash_buffer(HSTATE_T *hstate, const void *input, size_t len)
{
	xxh64_update(hstate, input, len);
}

/* Return the hash result as a hex string. Caller frees. */
char *
hash_result(HSTATE_T *hstate)
{
	unsigned char sum[HSIZE];
	hash_result_as_bytes(hstate, sum);
	return format_hash_as_string(sum, hash_get_len(hstate));
}

/* return the hash result as HSIZE binary bytes */
void
hash_result_as_bytes(HSTATE_T *hstate, void *out)
{
	unsigned long long *hash = out;
#if HSIZE == 16
	hash[1] = 0;
#endif
	hash[0] = xxh64_digest(hstate);
}

bool
hash_equal(HSTATE_T *hstate1, HSTATE_T *hstate2)
{
	return xxh64_digest(hstate1) == xxh64_digest(hstate2);
}

/*
 * Hash some data that is unlikely to occur in the input. The idea is twofold:
 *
 * - Delimit things like arguments from each other (e.g., so that -I -O2 and
 *   -I-O2 hash differently).
 * - Tag different types of hashed information so that it's possible to do
 *   conditional hashing of information in a safe way (e.g., if we want to hash
 *   information X if CCACHE_A is set and information Y if CCACHE_B is set,
 *   there should never be a hash collision risk).
 */
void
hash_delimiter(HSTATE_T *hstate, const char *type)
{
	hash_buffer(hstate, HASH_DELIMITER, sizeof(HASH_DELIMITER));
	hash_buffer(hstate, type, strlen(type) + 1); /* Include NUL. */
}

void
hash_string(HSTATE_T *hstate, const char *s)
{
	hash_buffer(hstate, s, strlen(s));
}

void
hash_string_length(HSTATE_T *hstate, const char *s, int length)
{
        hash_buffer(hstate, s, length);
}

void
hash_int(HSTATE_T *hstate, int x)
{
	hash_buffer(hstate, &x, sizeof(x));
}

/*
 * Add contents of an open file to the hash. Returns true on success, otherwise
 * false.
 */
bool
hash_fd(HSTATE_T *hstate, int fd)
{
	char buf[16384];
	ssize_t n;

	while ((n = read(fd, buf, sizeof(buf))) != 0) {
		if (n == -1 && errno != EINTR) {
			break;
		}
		if (n > 0) {
			hash_buffer(hstate, buf, n);
		}
	}
	return n == 0;
}

/*
 * Add contents of a file to the hash. Returns true on success, otherwise
 * false.
 */
bool
hash_file(HSTATE_T *hstate, const char *fname)
{
	int fd;
	bool ret;

	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd == -1) {
		return false;
	}

	ret = hash_fd(hstate, fd);
	close(fd);
	return ret;
}
