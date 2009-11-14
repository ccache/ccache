/*
   Copyright (C) Andrew Tridgell 2002

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
  simple front-end functions to mdfour code
*/

#include "ccache.h"
#include <stdio.h>

void hash_buffer(struct mdfour *md, const char *s, int len)
{
	mdfour_update(md, (unsigned char *)s, len);
}

void hash_start(struct mdfour *md)
{
	mdfour_begin(md);
}

void hash_string(struct mdfour *md, const char *s)
{
	hash_buffer(md, s, strlen(s));
}

void hash_int(struct mdfour *md, int x)
{
	hash_buffer(md, (char *)&x, sizeof(x));
}

/*
 * Add contents of an open file to the hash. Returns 1 on success, otherwise 0.
 */
int hash_fd(struct mdfour *md, int fd)
{
	char buf[1024];
	int n;

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		hash_buffer(md, buf, n);
	}
	if (n == 0) {
		return 1;
	} else {
		return 0;
	}
}

/*
 * Add contents of a file to the hash. Returns 1 on success, otherwise 0.
 */
int hash_file(struct mdfour *md, const char *fname)
{
	int fd;
	int ret;

	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd == -1) {
		return 0;
	}

	ret = hash_fd(md, fd);
	close(fd);
	return ret;
}

/* return the hash result as a hex string */
char *hash_result(struct mdfour *md)
{
	unsigned char sum[16];
	char *ret;
	int i;

	ret = x_malloc(53);
	hash_result_as_bytes(md, sum);

	for (i=0;i<16;i++) {
		sprintf(&ret[i*2], "%02x", (unsigned)sum[i]);
	}
	sprintf(&ret[i*2], "-%u", (unsigned)md->totalN);

	return ret;
}

/* return the hash result as 16 binary bytes */
void hash_result_as_bytes(struct mdfour *md, unsigned char *out)
{
	hash_buffer(md, NULL, 0);
	mdfour_result(md, out);
}
