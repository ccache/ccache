/*
 * Copyright (C) 2002 Andrew Tridgell
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <utime.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

static FILE *logfile;

static int init_log()
{
	extern char *cache_logfile;

	if (logfile) {
		return 1;
	}
	if (!cache_logfile) {
		return 0;
	}
	logfile = fopen(cache_logfile, "a");
	if (logfile) {
		return 1;
	} else {
		return 0;
	}
}

static void log_va_list(const char *format, va_list ap)
{
	if (!init_log()) {
		return;
	}

	fprintf(logfile, "[%-5d] ", getpid());
	vfprintf(logfile, format, ap);
}

/*
 * Log a message to the CCACHE_LOGFILE location without newline and without
 * flushing.
 */
void cc_log_no_newline(const char *format, ...)
{
	if (!init_log()) {
		return;
	}

	va_list ap;
	va_start(ap, format);
	log_va_list(format, ap);
	va_end(ap);
}

/*
 * Log a message to the CCACHE_LOGFILE location adding a newline and flushing.
 */
void cc_log(const char *format, ...)
{
	if (!init_log()) {
		return;
	}

	va_list ap;
	va_start(ap, format);
	log_va_list(format, ap);
	va_end(ap);
	fprintf(logfile, "\n");
	fflush(logfile);
}

void cc_log_executed_command(char **argv)
{
	if (!init_log()) {
		return;
	}

	cc_log_no_newline("Executing ");
	print_command(logfile, argv);
	fflush(logfile);
}

/* something went badly wrong! */
void fatal(const char *format, ...)
{
	va_list ap;
	extern char *cache_logfile;
	char msg[1000];

	va_start(ap, format);
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	if (cache_logfile) {
		if (!logfile) {
			logfile = fopen(cache_logfile, "a");
		}
		if (logfile) {
			fprintf(logfile, "[%-5d] FATAL: %s", getpid(), msg);
			fflush(logfile);
		}
	}

	fprintf(stderr, "ccache: FATAL: %s\n", msg);

	exit(1);
}

/*
 * Copy all data from fd_in to fd_out, decompressing data from fd_in if needed.
 */
void copy_fd(int fd_in, int fd_out)
{
	char buf[10240];
	int n;
	gzFile gz_in;

	gz_in = gzdopen(dup(fd_in), "rb");

	if (!gz_in) {
		fatal("Failed to copy fd");
	}

	while ((n = gzread(gz_in, buf, sizeof(buf))) > 0) {
		if (write(fd_out, buf, n) != n) {
			fatal("Failed to copy fd");
		}
	}
}

/*
 * Copy src to dest, decompressing src if needed. compress_dest decides whether
 * dest will be compressed.
 */
int copy_file(const char *src, const char *dest, int compress_dest)
{
	int fd_in = -1, fd_out = -1;
	gzFile gz_in = NULL, gz_out = NULL;
	char buf[10240];
	int n, ret;
	char *tmp_name;
	mode_t mask;
	struct stat st;
	int errnum;

	cc_log("Copying %s to %s (%s)",
	       src, dest, compress_dest ? "compressed": "uncompressed");

	/* open source file */
	fd_in = open(src, O_RDONLY);
	if (fd_in == -1) {
		cc_log("open error: %s", strerror(errno));
		return -1;
	}

	gz_in = gzdopen(fd_in, "rb");
	if (!gz_in) {
		cc_log("gzdopen(src) error: %s", strerror(errno));
		close(fd_in);
		return -1;
	}

	/* open destination file */
	x_asprintf(&tmp_name, "%s.%s.XXXXXX", dest, tmp_string());
	fd_out = mkstemp(tmp_name);
	if (fd_out == -1) {
		cc_log("mkstemp error: %s", strerror(errno));
		goto error;
	}

	if (compress_dest) {
		/*
		 * A gzip file occupies at least 20 bytes, so it will always
		 * occupy an entire filesystem block, even for empty files.
		 * Turn off compression for empty files to save some space.
		 */
		if (fstat(fd_in, &st) != 0) {
			cc_log("fstat error: %s", strerror(errno));
			goto error;
		}
		if (file_size(&st) == 0) {
			compress_dest = 0;
		}
	}

	if (compress_dest) {
		gz_out = gzdopen(dup(fd_out), "wb");
		if (!gz_out) {
			cc_log("gzdopen(dest) error: %s", strerror(errno));
			goto error;
		}
	}

	while ((n = gzread(gz_in, buf, sizeof(buf))) > 0) {
		if (compress_dest) {
			ret = gzwrite(gz_out, buf, n);
		} else {
			ret = write(fd_out, buf, n);
		}
		if (ret != n) {
			if (compress_dest) {
				cc_log("gzwrite error: %s (errno: %s)",
				       gzerror(gz_in, &errnum),
				       strerror(errno));
			} else {
				cc_log("write error: %s", strerror(errno));
			}
			goto error;
		}
	}
	if (n == 0 && !gzeof(gz_in)) {
		cc_log("gzread error: %s (errno: %s)",
		       gzerror(gz_in, &errnum), strerror(errno));
		gzclose(gz_in);
		if (gz_out) {
			gzclose(gz_out);
		}
		close(fd_out);
		unlink(tmp_name);
		free(tmp_name);
		return -1;
	}

	gzclose(gz_in);
	gz_in = NULL;
	if (gz_out) {
		gzclose(gz_out);
		gz_out = NULL;
	}

	/* get perms right on the tmp file */
	mask = umask(0);
	fchmod(fd_out, 0666 & ~mask);
	umask(mask);

	/* the close can fail on NFS if out of space */
	if (close(fd_out) == -1) {
		cc_log("close error: %s", strerror(errno));
		goto error;
	}

	unlink(dest);

	if (rename(tmp_name, dest) == -1) {
		cc_log("rename error: %s", strerror(errno));
		goto error;
	}

	free(tmp_name);

	return 0;

error:
	if (gz_in) {
		gzclose(gz_in);
	}
	if (gz_out) {
		gzclose(gz_out);
	}
	if (fd_out != -1) {
		close(fd_out);
	}
	unlink(tmp_name);
	free(tmp_name);
	return -1;
}

/* Run copy_file() and, if successful, delete the source file. */
int move_file(const char *src, const char *dest, int compress_dest)
{
	int ret;

	ret = copy_file(src, dest, compress_dest);
	if (ret != -1) {
		unlink(src);
	}
	return ret;
}

/*
 * Like move_file(), but assumes that src is uncompressed and that src and dest
 * are on the same file system.
 */
int
move_uncompressed_file(const char *src, const char *dest, int compress_dest)
{
	if (compress_dest) {
		return move_file(src, dest, compress_dest);
	} else {
		return rename(src, dest);
	}
}

/* test if a file is zlib compressed */
int test_if_compressed(const char *filename)
{
	FILE *f;

	f = fopen(filename, "rb");
	if (!f) {
		return 0;
	}

	/* test if file starts with 1F8B, which is zlib's
	 * magic number */
	if ((fgetc(f) != 0x1f) || (fgetc(f) != 0x8b)) {
		fclose(f);
		return 0;
	}

	fclose(f);
	return 1;
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
 * Return a static string with the current hostname.
 */
const char *get_hostname(void)
{
	static char hostname[200] = "";

	if (!hostname[0]) {
		strcpy(hostname, "unknown");
#if HAVE_GETHOSTNAME
		gethostname(hostname, sizeof(hostname)-1);
#endif
		hostname[sizeof(hostname)-1] = 0;
	}

	return hostname;
}

/*
 * Return a string to be used to distinguish temporary files. Also tries to
 * cope with NFS by adding the local hostname.
 */
const char *tmp_string(void)
{
	static char *ret;

	if (!ret) {
		x_asprintf(&ret, "%s.%u", get_hostname(), (unsigned)getpid());
	}

	return ret;
}

/* Return the hash result as a hex string. Caller frees. */
char *format_hash_as_string(const unsigned char *hash, unsigned size)
{
	char *ret;
	int i;

	ret = x_malloc(53);
	for (i = 0; i < 16; i++) {
		sprintf(&ret[i*2], "%02x", (unsigned) hash[i]);
	}
	sprintf(&ret[i*2], "-%u", size);

	return ret;
}

char const CACHEDIR_TAG[] =
	"Signature: 8a477f597d28d172789f06886806bc55\n"
	"# This file is a cache directory tag created by ccache.\n"
	"# For information about cache directory tags, see:\n"
	"#	http://www.brynosaurus.com/cachedir/\n";

int create_cachedirtag(const char *dir)
{
	char *filename;
	struct stat st;
	FILE *f;
	x_asprintf(&filename, "%s/CACHEDIR.TAG", dir);
	if (stat(filename, &st) == 0) {
		if (S_ISREG(st.st_mode)) {
			goto success;
		}
		errno = EEXIST;
		goto error;
	}
	f = fopen(filename, "w");
	if (!f) goto error;
	if (fwrite(CACHEDIR_TAG, sizeof(CACHEDIR_TAG)-1, 1, f) != 1) {
		goto error;
	}
	if (fclose(f)) goto error;
success:
	free(filename);
	return 0;
error:
	free(filename);
	return 1;
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
	if (vasprintf(ptr, format, ap) == -1) {
		fatal("Out of memory in x_asprintf");
	}
	va_end(ap);

	if (!*ptr) fatal("Out of memory in x_asprintf");
}

/*
  this is like strdup() but dies if the malloc fails
*/
char *x_strdup(const char *s)
{
	char *ret;
	ret = strdup(s);
	if (!ret) {
		fatal("Out of memory in x_strdup");
	}
	return ret;
}

/*
  this is like strndup() but dies if the malloc fails
*/
char *x_strndup(const char *s, size_t n)
{
	char *ret;
#ifndef HAVE_STRNDUP
	size_t m;

	if (!s)
		return NULL;
	m = 0;
	while (m < n && s[m]) {
		m++;
	}
	ret = malloc(m + 1);
	if (ret) {
		memcpy(ret, s, m);
		ret[m] = '\0';
	}
#else
	ret = strndup(s, n);
#endif
	if (!ret) {
		fatal("Out of memory in x_strndup");
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
		fatal("Out of memory in x_malloc");
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
	p2 = realloc(ptr, size);
	if (!p2) {
		fatal("Out of memory in x_realloc");
	}
	return p2;
}


/*
 * This is like x_asprintf() but frees *ptr if *ptr != NULL.
 */
void x_asprintf2(char **ptr, const char *format, ...)
{
	char *saved = *ptr;
	va_list ap;

	*ptr = NULL;
	va_start(ap, format);
	if (vasprintf(ptr, format, ap) == -1) {
		fatal("Out of memory in x_asprintf2");
	}
	va_end(ap);

	if (!ptr) fatal("Out of memory in x_asprintf2");
	if (saved) {
		free(saved);
	}
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

/*
 * Return the file extension of a path as a pointer into path. If path has no
 * file extension, the empty string is returned.
 */
const char *get_extension(const char *path)
{
	size_t len = strlen(path);
	const char *p;

	for (p = &path[len - 1]; p >= path; --p) {
		if (*p == '.') {
			return p;
		}
		if (*p == '/') {
			break;
		}
	}
	return &path[len];
}

/*
 * Return a string containing the given path without the filename extension.
 * Caller frees.
 */
char *remove_extension(const char *path)
{
	return x_strndup(path, strlen(path) - strlen(get_extension(path)));
}

static int lock_fd(int fd, short type)
{
	struct flock fl;
	int ret;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 1;
	fl.l_pid = 0;

	/* not sure why we would be getting a signal here,
	   but one user claimed it is possible */
	do {
		ret = fcntl(fd, F_SETLKW, &fl);
	} while (ret == -1 && errno == EINTR);
	return ret;
}

int read_lock_fd(int fd)
{
	return lock_fd(fd, F_RDLCK);
}

int write_lock_fd(int fd)
{
	return lock_fd(fd, F_WRLCK);
}

/* return size on disk of a file */
size_t file_size(struct stat *st)
{
	size_t size = st->st_blocks * 512;
	if ((size_t)st->st_size > size) {
		/* probably a broken stat() call ... */
		size = (st->st_size + 1023) & ~1023;
	}
	return size;
}


/* a safe open/create for read-write */
int safe_open(const char *fname)
{
	int fd = open(fname, O_RDWR|O_BINARY);
	if (fd == -1 && errno == ENOENT) {
		fd = open(fname, O_RDWR|O_CREAT|O_EXCL|O_BINARY, 0666);
		if (fd == -1 && errno == EEXIST) {
			fd = open(fname, O_RDWR|O_BINARY);
		}
	}
	return fd;
}

/* Format a size as a human-readable string. Caller frees. */
char *format_size(size_t v)
{
	char *s;
	if (v >= 1024*1024) {
		x_asprintf(&s, "%.1f Gbytes", v/((double)(1024*1024)));
	} else if (v >= 1024) {
		x_asprintf(&s, "%.1f Mbytes", v/((double)(1024)));
	} else {
		x_asprintf(&s, "%.0f Kbytes", (double)v);
	}
	return s;
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

#if HAVE_REALPATH
	p = realpath(path, ret);
#else
	/* yes, there are such systems. This replacement relies on
	   the fact that when we call x_realpath we only care about symlinks */
	{
		int len = readlink(path, ret, maxlen-1);
		if (len == -1) {
			free(ret);
			return NULL;
		}
		ret[len] = 0;
		p = ret;
	}
#endif
	if (p) {
		p = x_strdup(p);
		free(ret);
		return p;
	}
	free(ret);
	return NULL;
}

/* a getcwd that will returns an allocated buffer */
char *gnu_getcwd(void)
{
	unsigned size = 128;

	while (1) {
		char *buffer = (char *)x_malloc(size);
		if (getcwd(buffer, size) == buffer) {
			return buffer;
		}
		free(buffer);
		if (errno != ERANGE) {
			return 0;
		}
		size *= 2;
	}
}

#ifndef HAVE_MKSTEMP
/* cheap and nasty mkstemp replacement */
int mkstemp(char *template)
{
	mktemp(template);
	return open(template, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
}
#endif


/* create an empty file */
int create_empty_file(const char *fname)
{
	int fd;

	fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL|O_BINARY, 0666);
	if (fd == -1) {
		return -1;
	}
	close(fd);
	return 0;
}

/*
 * Return current user's home directory, or NULL if it can't be determined.
 */
const char *get_home_directory(void)
{
	const char *p = getenv("HOME");
	if (p) {
		return p;
	}
#ifdef HAVE_GETPWUID
	{
		struct passwd *pwd = getpwuid(getuid());
		if (pwd) {
			return pwd->pw_dir;
		}
	}
#endif
	return NULL;
}

/*
 * Get the current directory by reading $PWD. If $PWD isn't sane, gnu_getcwd()
 * is used.
 */
char *get_cwd(void)
{
	char *pwd;
	char *cwd;
	struct stat st_pwd;
	struct stat st_cwd;

	cwd = gnu_getcwd();
	pwd = getenv("PWD");
	if (!pwd) {
		return cwd;
	}
	if (stat(pwd, &st_pwd) != 0) {
		return cwd;
	}
	if (stat(cwd, &st_cwd) != 0) {
		return cwd;
	}
	if (st_pwd.st_dev == st_cwd.st_dev && st_pwd.st_ino == st_cwd.st_ino) {
		return x_strdup(pwd);
	} else {
		return cwd;
	}
}

/*
 * Compute the length of the longest directory path that is common to two
 * strings.
 */
size_t common_dir_prefix_length(const char *s1, const char *s2)
{
	const char *p1 = s1;
	const char *p2 = s2;

	while (*p1 && *p2 && *p1 == *p2) {
		++p1;
		++p2;
	}
	while (p1 > s1 && ((*p1 && *p1 != '/' ) || (*p2 && *p2 != '/'))) {
		p1--;
		p2--;
	}
	return p1 - s1;
}

/*
 * Compute a relative path from from to to. Caller frees.
 */
char *get_relative_path(const char *from, const char *to)
{
	size_t common_prefix_len;
	int i;
	const char *p;
	char *result;

	if (!*to || *to != '/') {
		return x_strdup(to);
	}

	result = x_strdup("");
	common_prefix_len = common_dir_prefix_length(from, to);
	for (p = from + common_prefix_len; *p; p++) {
		if (*p == '/') {
			x_asprintf2(&result, "../%s", result);
		}
	}
	if (strlen(to) > common_prefix_len) {
		p = to + common_prefix_len + 1;
		while (*p == '/') {
			p++;
		}
		x_asprintf2(&result, "%s%s", result, p);
	}
	i = strlen(result) - 1;
	while (i >= 0 && result[i] == '/') {
		result[i] = '\0';
		i--;
	}
	if (strcmp(result, "") == 0) {
		free(result);
		result = x_strdup(".");
	}
	return result;
}

/*
 * Update the modification time of a file in the cache to save it from LRU
 * cleanup.
 */
void
update_mtime(const char *path)
{
#ifdef HAVE_UTIMES
	utimes(path, NULL);
#else
	utime(path, NULL);
#endif
}
