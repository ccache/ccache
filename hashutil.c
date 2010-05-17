/*
 * Copyright (C) 2009-2010 Joel Rosdahl
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
#include "hashutil.h"
#include "murmurhashneutral2.h"

#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

unsigned int hash_from_string(void *str)
{
	return murmurhashneutral2(str, strlen((const char *)str), 0);
}

int strings_equal(void *str1, void *str2)
{
	return strcmp((const char *)str1, (const char *)str2) == 0;
}

int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2)
{
	return memcmp(fh1->hash, fh2->hash, 16) == 0
		&& fh1->size == fh2->size;
}

#define HASH(ch)							\
	do {								\
		hashbuf[hashbuflen] = ch;				\
		hashbuflen++;						\
		if (hashbuflen == sizeof(hashbuf)) {			\
			hash_buffer(hash, hashbuf, sizeof(hashbuf));	\
			hashbuflen = 0;					\
		}							\
	} while (0)

/*
 * Hash a string ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
 * results.
 */
int
hash_source_code_string(
	struct mdfour *hash, const char *str, size_t len, const char *path)
{
	const char *p;
	const char *end;
	char hashbuf[64];
	size_t hashbuflen = 0;
	int result = HASH_SOURCE_CODE_OK;
	extern unsigned sloppiness;

	p = str;
	end = str + len;
	while (1) {
		if (p >= end) {
			goto end;
		}
		switch (*p) {
		/* Potential start of comment. */
		case '/':
			if (p+1 == end) {
				break;
			}
			switch (*(p+1)) {
			case '*':
				HASH(' '); /* Don't paste tokens together when
					    * removing the comment. */
				p += 2;
				while (p+1 < end
				       && (*p != '*' || *(p+1) != '/')) {
					if (*p == '\n') {
						/* Keep line numbers. */
						HASH('\n');
					}
					p++;
				}
				if (p+1 == end) {
					goto end;
				}
				p += 2;
				continue;

			case '/':
				p += 2;
				while (p < end
				       && (*p != '\n' || *(p-1) == '\\')) {
					p++;
				}
				continue;

			default:
				break;
			}
			break;

		/* Start of string. */
		case '"':
			HASH(*p);
			p++;
			while (p < end && (*p != '"' || *(p-1) == '\\')) {
				HASH(*p);
				p++;
			}
			if (p == end) {
				goto end;
			}
			break;

		/* Potential start of volatile macro. */
		case '_':
			if (p + 7 < end
			    && p[1] == '_' && p[5] == 'E'
			    && p[6] == '_' && p[7] == '_') {
				if (p[2] == 'D' && p[3] == 'A'
				    && p[4] == 'T') {
					result |= HASH_SOURCE_CODE_FOUND_DATE;
				} else if (p[2] == 'T' && p[3] == 'I'
					   && p[4] == 'M') {
					result |= HASH_SOURCE_CODE_FOUND_TIME;
				}
				/*
				 * Of course, we can't be sure that we have
				 * found a __{DATE,TIME}__ that's actually
				 * used, but better safe than sorry. And if you
				 * do something like
				 *
				 * #define TIME __TI ## ME__
				 *
				 * in your code, you deserve to get a false
				 * cache hit.
				 */
			}
			break;

		default:
			break;
		}

		HASH(*p);
		p++;
	}

end:
	hash_buffer(hash, hashbuf, hashbuflen);

	if (sloppiness & SLOPPY_TIME_MACROS) {
		return 0;
	}
	if (result & HASH_SOURCE_CODE_FOUND_DATE) {
		/*
		 * Make sure that the hash sum changes if the (potential)
		 * expansion of __DATE__ changes.
		 */
		cc_log("Found __DATE__ in %s", path);
		time_t t = time(NULL);
		struct tm *now = localtime(&t);
		hash_delimiter(hash, "date");
		hash_buffer(hash, &now->tm_year, sizeof(now->tm_year));
		hash_buffer(hash, &now->tm_mon, sizeof(now->tm_mon));
		hash_buffer(hash, &now->tm_mday, sizeof(now->tm_mday));
	}
	if (result & HASH_SOURCE_CODE_FOUND_TIME) {
		/*
		 * We don't know for sure that the program actually uses the
		 * __TIME__ macro, but we have to assume it anyway and hash the
		 * time stamp. However, that's not very useful since the chance
		 * that we get a cache hit later the same second should be
		 * quite slim... So, just signal back to the caller that
		 * __TIME__ has been found so that the direct mode can be
		 * disabled.
		 */
		cc_log("Found __TIME__ in %s", path);
	}

	return result;
}

/*
 * Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
 * results.
 */
int
hash_source_code_file(struct mdfour *hash, const char *path)
{
	int fd;
	struct stat st;
	char *data;
	int result;

	fd = open(path, O_RDONLY|O_BINARY);
	if (fd == -1) {
		cc_log("Failed to open %s", path);
		return HASH_SOURCE_CODE_ERROR;
	}
	if (fstat(fd, &st) == -1) {
		cc_log("Failed to fstat %s", path);
		close(fd);
		return HASH_SOURCE_CODE_ERROR;
	}
	if (st.st_size == 0) {
		close(fd);
		return HASH_SOURCE_CODE_OK;
	}
	data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (data == (void *)-1) {
		cc_log("Failed to mmap %s", path);
		return HASH_SOURCE_CODE_ERROR;
	}

	result = hash_source_code_string(hash, data, st.st_size, path);
	munmap(data, st.st_size);
	return result;
}
