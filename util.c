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
	va_list ap;
	extern char *cache_logfile;

	if (!cache_logfile) return;

	if (!logfile) logfile = fopen(cache_logfile, "a");
	if (!logfile) return;
	
	va_start(ap, format);
	vfprintf(logfile, format, ap);
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
	char buf[10240];
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

	*ptr = NULL;
	va_start(ap, format);
	vasprintf(ptr, format, ap);
	va_end(ap);
	
	if (!ptr) fatal("out of memory in x_asprintf");
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
  this is like realloc() but dies if the malloc fails
*/
void *x_realloc(void *ptr, size_t size)
{
	void *p2;
	if (!ptr) return x_malloc(size);
	p2 = malloc(size);
	if (!p2) {
		fatal("out of memory in x_realloc");
	}
	if (ptr) {
		memcpy(p2, ptr, size);
		free(ptr);
	}
	return p2;
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

		if (strlen(de->d_name) == 0) continue;

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


/* return the base name of a file - caller frees */
char *basename(const char *s)
{
	char *p = strrchr(s, '/');
	if (p) {
		return x_strdup(p+1);
	} 

	return x_strdup(s);
}

/* return the dir name of a file - caller frees */
char *dirname(char *s)
{
	char *p;
	s = x_strdup(s);
	p = strrchr(s, '/');
	if (p) {
		*p = 0;
	} 
	return s;
}

int lock_fd(int fd)
{
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 1;
	fl.l_pid = 0;

	return fcntl(fd, F_SETLKW, &fl);
}

/* return size on disk of a file */
size_t file_size(struct stat *st)
{
	size_t size = st->st_blocks * 512;
	if (st->st_size > size) {
		/* probably a broken stat() call ... */
		size = (st->st_size + 1023) & ~1023;
	}
	return size;
}


/* a safe open/create for read-write */
int safe_open(const char *fname)
{
	int fd = open(fname, O_RDWR);
	if (fd == -1 && errno == ENOENT) {
		fd = open(fname, O_RDWR|O_CREAT|O_EXCL, 0666);
		if (fd == -1 && errno == EEXIST) {
			fd = open(fname, O_RDWR);
		}
	}
	return fd;
}

/* display a kilobyte unsigned value in M, k or G */
void display_size(unsigned v)
{
	if (v > 1024*1024) {
		printf("%8.1f Gbytes", v/((double)(1024*1024)));
	} else if (v > 1024) {
		printf("%8.1f Mbytes", v/((double)(1024)));
	} else {
		printf("%8u Kbytes", v);
	}
}

/* return a value in multiples of 1024 give a string that can end
   in K, M or G
*/
size_t value_units(const char *s)
{
	char m;
	double v = atof(s);
	m = s[strlen(s)-1];
	switch (m) {
	case 'G':
	case 'g':
	default:
		v *= 1024*1024;
		break;
	case 'M':
	case 'm':
		v *= 1024;
		break;
	case 'K':
	case 'k':
		v *= 1;
		break;
	}
	return (size_t)v;
}


/*
  a sane realpath() function, trying to cope with stupid path limits and 
  a broken API
*/
char *x_realpath(const char *path)
{
	int maxlen;
	char *ret, *p;
#ifdef PATH_MAX
	maxlen = PATH_MAX;
#elif defined(MAXPATHLEN)
	maxlen = MAXPATHLEN;
#elif defined(_PC_PATH_MAX)
	maxlen = pathconf(path, _PC_PATH_MAX);
#endif
	if (maxlen < 4096) maxlen = 4096;
	
	ret = x_malloc(maxlen);
	
	p = realpath(path, ret);
	if (p) {
		p = x_strdup(p);
		free(ret);
		return p;
	}
	free(ret);
	return NULL;
}
