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

#include "ccache.h"

static FILE *logfile;

/* log a message to the CCACHE_LOGFILE location */
void cc_log(const char *format, ...)
{
	int ret;
	va_list ap;
	extern char *cache_logfile;

	if (!cache_logfile) return;

	if (!logfile) logfile = fopen(cache_logfile, "a");
	if (!logfile) return;
	
	va_start(ap, format);
	ret = vfprintf(logfile, format, ap);
	va_end(ap);
	fflush(logfile);
}

/* something went badly wrong! */
void fatal(const char *msg)
{
	cc_log("FATAL: %s\n", msg);
	exit(1);
}

/* copy all data from one file descriptor to another */
void copy_fd(int fd_in, int fd_out)
{
	char buf[1024];
	int n;

	while ((n = read(fd_in, buf, sizeof(buf))) > 0) {
		if (write(fd_out, buf, n) != n) {
			fatal("Failed to copy fd");
		}
	}
}

/* copy a file - used when hard links don't work */
int copy_file(const char *src, const char *dest)
{
	int fd1, fd2;
	char buf[10240];
	int n;

	fd1 = open(src, O_RDONLY);
	if (fd1 == -1) return -1;

	unlink(dest);
	fd2 = open(dest, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0666);
	if (fd2 == -1) {
		close(fd1);
		return -1;
	}

	while ((n = read(fd1, buf, sizeof(buf))) > 0) {
		if (write(fd2, buf, n) != n) {
			close(fd2);
			close(fd1);
			unlink(dest);
			return -1;
		}
	}

	close(fd2);
	close(fd1);
	return 0;
}


/* make sure a directory exists */
int create_dir(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return 0;
		}
		errno = ENOTDIR;
		return 1;
	}
	if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
		return 1;
	}
	return 0;
}

/*
  this is like asprintf() but dies if the malloc fails
  note that we use vsnprintf in a rather poor way to make this more portable
*/
void x_asprintf(char **ptr, const char *format, ...)
{
	va_list ap;
	unsigned ret;
	static char tmp[1024];

	*ptr = NULL;
	va_start(ap, format);
	ret = vsnprintf(tmp, sizeof(tmp), format, ap);
	va_end(ap);

	if (ret >= sizeof(tmp)-1) {
		fatal("vsnprintf - too long\n");
	}

	*ptr = x_strdup(tmp);
}

/*
  this is like strdup() but dies if the malloc fails
*/
char *x_strdup(const char *s)
{
	char *ret;
	ret = strdup(s);
	if (!ret) {
		fatal("out of memory in strdup\n");
	}
	return ret;
}

/*
  this is like malloc() but dies if the malloc fails
*/
void *x_malloc(size_t size)
{
	void *ret;
	ret = malloc(size);
	if (!ret) {
		fatal("out of memory in malloc\n");
	}
	return ret;
}

/*
  this is like strdup() but dies if the malloc fails
*/
void *x_realloc(void *ptr, size_t size)
{
	if (!ptr) return x_malloc(size);
	ptr = realloc(ptr, size);
	if (!ptr) {
		fatal("out of memory in x_realloc");
	}
	return ptr;
}


/* 
   revsusive directory traversal - used for cleanup
   fn() is called on all files/dirs in the tree
 */
void traverse(const char *dir, void (*fn)(const char *, struct stat *))
{
	DIR *d;
	struct dirent *de;

	d = opendir(dir);
	if (!d) return;

	while ((de = readdir(d))) {
		char *fname;
		struct stat st;

		if (strcmp(de->d_name,".") == 0) continue;
		if (strcmp(de->d_name,"..") == 0) continue;

		x_asprintf(&fname, "%s/%s", dir, de->d_name);
		if (lstat(fname, &st)) {
			if (errno != ENOENT) {
				perror(fname);
			}
			free(fname);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			traverse(fname, fn);
		}

		fn(fname, &st);
		free(fname);
	}

	closedir(d);
}
