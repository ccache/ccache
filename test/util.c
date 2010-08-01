/*
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

int
path_exists(const char *path)
{
	struct stat st;
	return lstat(path, &st) == 0;
}

int
is_symlink(const char *path)
{
	struct stat st;
	return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

void
create_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	if (!f || fputs(content, f) < 0) {
		fprintf(stderr, "create_file: %s: %s\n", path, strerror(errno));
	}
	if (f) {
		fclose(f);
	}
}

/* Return the content of a text file, or NULL on error. Caller frees. */
char *
read_file(const char *path)
{
	int fd, ret;
	size_t pos = 0, allocated = 1024;
	char *result = malloc(allocated);

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		free(result);
		return NULL;
	}
	ret = 0;
	do {
		pos += ret;
		if (pos > allocated / 2) {
			allocated *= 2;
			result = realloc(result, allocated);
		}
	} while ((ret = read(fd, result + pos, allocated - pos - 1)) > 0);
	close(fd);
	if (ret == -1) {
		free(result);
		return NULL;
	}
	result[pos] = '\0';

	return result;
}
