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

void hash_include_file_string(
	struct mdfour *hash, const char *str, size_t len)
{
	const char *p;
	const char *end;
	char hashbuf[64];
	size_t hashbuflen = 0;

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

		default:
			break;
		}

		HASH(*p);
		p++;
	}

end:
	hash_buffer(hash, hashbuf, hashbuflen);
}

/*
 * Add contents of a file to a hash, but don't hash comments. Returns 1 on
 * success, otherwise 0.
 */
int hash_include_file(struct mdfour *hash, const char *path)
{
	int fd;
	struct stat st;
	char *data;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		return 0;
	}
	if (fstat(fd, &st) == -1) {
		close(fd);
		return 0;
	}
	if (st.st_size == 0) {
		close(fd);
		return 1;
	}
	data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (data == (void *)-1) {
		return 0;
	}

	hash_include_file_string(hash, data, st.st_size);

	munmap(data, st.st_size);
	return 1;
}
