/*
 * Copyright (C) 2002 Andrew Tridgell
 * Copyright (C) 2010 Joel Rosdahl
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
#include "mdfour.h"
#include "murmurhashneutral2.h"

#define HASH_DELIMITER "\000cCaChE"

unsigned
hash_simple(const void* input, size_t length, unsigned seed) {
        return murmurhashneutral2(input, length, seed);
}

#if HNAME == mdfour
size_t
hash_get_len(struct hstate *hstate)
{
        struct mdfour *md = (struct mdfour *) hstate;
	return (size_t) md->totalN;
}

void
hash_start(struct hstate *hstate)
{
        struct mdfour *md = (struct mdfour *) hstate;
	mdfour_begin(md);
}

void
hash_buffer(struct hstate *hstate, const void *s, size_t len)
{
        struct mdfour *md = (struct mdfour *) hstate;
	mdfour_update(md, (unsigned char *)s, len);
}

/* return the hash result as 16 binary bytes */
void
hash_result_as_bytes(struct hstate *hstate, void *out)
{
        struct mdfour *md = (struct mdfour *) hstate;
        hash_buffer(hstate, NULL, 0);
        mdfour_result(md, out);
}
#endif

/* Return the hash result as a hex string. Caller frees. */
char *
hash_result(struct hstate *hstate)
{
	unsigned char sum[HSIZE];

	hash_result_as_bytes(hstate, sum);
	return format_hash_as_string(sum, hash_get_len(hstate));
}

bool
hash_equal(struct hstate *hstate1, struct hstate *hstate2)
{
	unsigned char sum1[HSIZE], sum2[HSIZE];
	hash_result_as_bytes(hstate1, sum1);
	hash_result_as_bytes(hstate2, sum2);
	return memcmp(sum1, sum2, sizeof(sum1)) == 0;
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
hash_delimiter(struct hstate *hstate, const char *type)
{
	hash_buffer(hstate, HASH_DELIMITER, sizeof(HASH_DELIMITER));
	hash_buffer(hstate, type, strlen(type) + 1); /* Include NUL. */
}

void
hash_string(struct hstate *hstate, const char *s)
{
	hash_string_length(hstate, s, strlen(s));
}

void
hash_string_length(struct hstate *hstate, const char *s, int length)
{
        hash_buffer(hstate, s, length);
}

void
hash_int(struct hstate *hstate, int x)
{
	hash_buffer(hstate, (char *)&x, sizeof(x));
}

/*
 * Add contents of an open file to the hash. Returns true on success, otherwise
 * false.
 */
bool
hash_fd(struct hstate *hstate, int fd)
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
hash_file(struct hstate *hstate, const char *fname)
{
	int fd;
	bool ret;

	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd == -1) {
		cc_log("Failed to open %s: %s", fname, strerror(errno));
		return false;
	}

	ret = hash_fd(hstate, fd);
	close(fd);
	return ret;
}
