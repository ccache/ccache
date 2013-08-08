/*
 * Copyright (C) 2002 Andrew Tridgell
 * Copyright (C) 2009-2013 Joel Rosdahl
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

#include <zlib.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <sys/locking.h>
#endif

static FILE *logfile;

static bool
init_log(void)
{
	extern struct conf *conf;

	if (logfile) {
		return true;
	}
	assert(conf);
	if (str_eq(conf->log_file, "")) {
		return false;
	}
	logfile = fopen(conf->log_file, "a");
	if (logfile) {
#ifndef _WIN32
		int fd = fileno(logfile);
		int flags = fcntl(fd, F_GETFD, 0);
		if (flags >= 0) {
			fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
		}
#endif
		return true;
	} else {
		return false;
	}
}

static void
log_prefix(bool log_updated_time)
{
#ifdef HAVE_GETTIMEOFDAY
	char timestamp[100];
	struct timeval tv;
	struct tm *tm;
	static char prefix[200];

	if (log_updated_time) {
		gettimeofday(&tv, NULL);
#ifdef __MINGW64_VERSION_MAJOR
		tm = _localtime32(&tv.tv_sec);
#else
		tm = localtime(&tv.tv_sec);
#endif
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm);
		snprintf(prefix, sizeof(prefix),
		         "[%s.%06d %-5d] ", timestamp, (int)tv.tv_usec, (int)getpid());
	}
	fputs(prefix, logfile);
#else
	fprintf(logfile, "[%-5d] ", (int)getpid());
#endif
}

static long
path_max(const char *path)
{
#ifdef PATH_MAX
	(void)path;
	return PATH_MAX;
#elif defined(MAXPATHLEN)
	(void)path;
	return MAXPATHLEN;
#elif defined(_PC_PATH_MAX)
	long maxlen = pathconf(path, _PC_PATH_MAX);
	if (maxlen >= 4096) {
		return maxlen;
	} else {
		return 4096;
	}
#endif
}

static void
vlog(const char *format, va_list ap, bool log_updated_time)
{
	if (!init_log()) {
		return;
	}

	log_prefix(log_updated_time);
	vfprintf(logfile, format, ap);
	fprintf(logfile, "\n");
}

/*
 * Write a message to the log file (adding a newline) and flush.
 */
void
cc_vlog(const char *format, va_list ap)
{
	vlog(format, ap, true);
}

/*
 * Write a message to the log file (adding a newline) and flush.
 */
void
cc_log(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vlog(format, ap, true);
	va_end(ap);
	if (logfile) {
		fflush(logfile);
	}
}

/*
 * Write a message to the log file (adding a newline) without flushing and with
 * a reused timestamp.
 */
void
cc_bulklog(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vlog(format, ap, false);
	va_end(ap);
}

/*
 * Log an executed command to the CCACHE_LOGFILE location.
 */
void
cc_log_argv(const char *prefix, char **argv)
{
	if (!init_log()) {
		return;
	}

	log_prefix(true);
	fputs(prefix, logfile);
	print_command(logfile, argv);
	fflush(logfile);
}

/* something went badly wrong! */
void
fatal(const char *format, ...)
{
	va_list ap;
	char msg[1000];

	va_start(ap, format);
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	cc_log("FATAL: %s", msg);
	fprintf(stderr, "ccache: error: %s\n", msg);

	exit(1);
}

/*
 * Copy all data from fd_in to fd_out, decompressing data from fd_in if needed.
 */
void
copy_fd(int fd_in, int fd_out)
{
	char buf[10240];
	int n;
	gzFile gz_in;

	gz_in = gzdopen(dup(fd_in), "rb");

	if (!gz_in) {
		fatal("Failed to copy fd");
	}

	while ((n = gzread(gz_in, buf, sizeof(buf))) > 0) {
		ssize_t count, written = 0;
		do {
			count = write(fd_out, buf + written, n - written);
			if (count == -1) {
				if (errno != EAGAIN && errno != EINTR) {
					fatal("Failed to copy fd");
				}
			} else {
				written += count;
			}
		} while (written < n);
	}

	gzclose(gz_in);
}

#ifndef HAVE_MKSTEMP
/* cheap and nasty mkstemp replacement */
int
mkstemp(char *template)
{
	mktemp(template);
	return open(template, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
}
#endif

/*
 * Copy src to dest, decompressing src if needed. compress_level > 0 decides
 * whether dest will be compressed, and with which compression level.
 */
int
copy_file(const char *src, const char *dest, int compress_level)
{
	int fd_in = -1, fd_out = -1;
	gzFile gz_in = NULL, gz_out = NULL;
	char buf[10240];
	int n, written;
	char *tmp_name;
#ifndef _WIN32
	mode_t mask;
#endif
	struct stat st;
	int errnum;

	tmp_name = format("%s.%s.XXXXXX", dest, tmp_string());

	/* open destination file */
	fd_out = mkstemp(tmp_name);
	if (fd_out == -1) {
		cc_log("mkstemp error: %s", strerror(errno));
		goto error;
	}

	cc_log("Copying %s to %s via %s (%scompressed)",
	       src, dest, tmp_name, compress_level > 0 ? "" : "un");

	/* open source file */
	fd_in = open(src, O_RDONLY | O_BINARY);
	if (fd_in == -1) {
		cc_log("open error: %s", strerror(errno));
		goto error;
	}

	gz_in = gzdopen(fd_in, "rb");
	if (!gz_in) {
		cc_log("gzdopen(src) error: %s", strerror(errno));
		close(fd_in);
		goto error;
	}

	if (compress_level > 0) {
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
			compress_level = 0;
		}
	}

	if (compress_level > 0) {
		gz_out = gzdopen(dup(fd_out), "wb");
		if (!gz_out) {
			cc_log("gzdopen(dest) error: %s", strerror(errno));
			goto error;
		}
		gzsetparams(gz_out, compress_level, Z_DEFAULT_STRATEGY);
	}

	while ((n = gzread(gz_in, buf, sizeof(buf))) > 0) {
		if (compress_level > 0) {
			written = gzwrite(gz_out, buf, n);
		} else {
			ssize_t count;
			written = 0;
			do {
				count = write(fd_out, buf + written, n - written);
				if (count == -1 && errno != EINTR) {
					break;
				}
				written += count;
			} while (written < n);
		}
		if (written != n) {
			if (compress_level > 0) {
				cc_log("gzwrite error: %s (errno: %s)",
				       gzerror(gz_in, &errnum),
				       strerror(errno));
			} else {
				cc_log("write error: %s", strerror(errno));
			}
			goto error;
		}
	}

	/*
	 * gzeof won't tell if there's an error in the trailing CRC, so we must check
	 * gzerror before considering everything OK.
	 */
	gzerror(gz_in, &errnum);
	if (!gzeof(gz_in) || (errnum != Z_OK && errnum != Z_STREAM_END)) {
		cc_log("gzread error: %s (errno: %s)",
		       gzerror(gz_in, &errnum), strerror(errno));
		gzclose(gz_in);
		if (gz_out) {
			gzclose(gz_out);
		}
		close(fd_out);
		tmp_unlink(tmp_name);
		free(tmp_name);
		return -1;
	}

	gzclose(gz_in);
	gz_in = NULL;
	if (gz_out) {
		gzclose(gz_out);
		gz_out = NULL;
	}

#ifndef _WIN32
	/* get perms right on the tmp file */
	mask = umask(0);
	fchmod(fd_out, 0666 & ~mask);
	umask(mask);
#endif

	/* the close can fail on NFS if out of space */
	if (close(fd_out) == -1) {
		cc_log("close error: %s", strerror(errno));
		goto error;
	}

	if (x_rename(tmp_name, dest) == -1) {
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
	tmp_unlink(tmp_name);
	free(tmp_name);
	return -1;
}

/* Run copy_file() and, if successful, delete the source file. */
int
move_file(const char *src, const char *dest, int compress_level)
{
	int ret;

	ret = copy_file(src, dest, compress_level);
	if (ret != -1) {
		x_unlink(src);
	}
	return ret;
}

/*
 * Like move_file(), but assumes that src is uncompressed and that src and dest
 * are on the same file system.
 */
int
move_uncompressed_file(const char *src, const char *dest, int compress_level)
{
	if (compress_level > 0) {
		return move_file(src, dest, compress_level);
	} else {
		return x_rename(src, dest);
	}
}

/* test if a file is zlib compressed */
bool
file_is_compressed(const char *filename)
{
	FILE *f;

	f = fopen(filename, "rb");
	if (!f) {
		return false;
	}

	/* test if file starts with 1F8B, which is zlib's
	 * magic number */
	if ((fgetc(f) != 0x1f) || (fgetc(f) != 0x8b)) {
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

/* make sure a directory exists */
int
create_dir(const char *dir)
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

/* Create directories leading to path. Returns 0 on success, otherwise -1. */
int
create_parent_dirs(const char *path)
{
	struct stat st;
	int res;
	char *parent = dirname(path);

	if (stat(parent, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			res = 0;
		} else {
			res = -1;
			errno = ENOTDIR;
		}
	} else {
		res = create_parent_dirs(parent);
		if (res == 0) {
			res = mkdir(parent, 0777);
			/* Have to handle the condition of the directory already existing because
			 * the file system could have changed in between calling stat and
			 * actually creating the directory. This can happen when there are
			 * multiple instances of ccache running and trying to create the same
			 * directory chain, which usually is the case when the cache root does
			 * not initially exist. As long as one of the processes creates the
			 * directories then our condition is satisfied and we avoid a race
			 * condition.
			 */
			if (res != 0 && errno == EEXIST) {
				res = 0;
			}
		} else {
			res = -1;
		}
	}
	free(parent);
	return res;
}

/*
 * Return a static string with the current hostname.
 */
const char *
get_hostname(void)
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
const char *
tmp_string(void)
{
	static char *ret;

	if (!ret) {
		ret = format("%s.%u", get_hostname(), (unsigned)getpid());
	}

	return ret;
}

/*
 * Return the hash result as a hex string. Size -1 means don't include size
 * suffix. Caller frees.
 */
char *
format_hash_as_string(const unsigned char *hash, int size)
{
	char *ret;
	int i;

	ret = x_malloc(53);
	for (i = 0; i < 16; i++) {
		sprintf(&ret[i*2], "%02x", (unsigned) hash[i]);
	}
	if (size >= 0) {
		sprintf(&ret[i*2], "-%u", size);
	}

	return ret;
}

char const CACHEDIR_TAG[] =
	"Signature: 8a477f597d28d172789f06886806bc55\n"
	"# This file is a cache directory tag created by ccache.\n"
	"# For information about cache directory tags, see:\n"
	"#	http://www.brynosaurus.com/cachedir/\n";

int
create_cachedirtag(const char *dir)
{
	struct stat st;
	FILE *f;
	char *filename = format("%s/CACHEDIR.TAG", dir);
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
		fclose(f);
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

/* Construct a string according to a format. Caller frees. */
char *
format(const char *format, ...)
{
	va_list ap;
	char *ptr = NULL;

	va_start(ap, format);
	if (vasprintf(&ptr, format, ap) == -1) {
		fatal("Out of memory in format");
	}
	va_end(ap);

	if (!*ptr) fatal("Internal error in format");
	return ptr;
}

/*
  this is like strdup() but dies if the malloc fails
*/
char *
x_strdup(const char *s)
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
char *
x_strndup(const char *s, size_t n)
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
		fatal("x_strndup: Could not allocate %lu bytes", (unsigned long)n);
	}
	return ret;
}

/*
  this is like malloc() but dies if the malloc fails
*/
void *
x_malloc(size_t size)
{
	void *ret;
	if (size == 0) {
		/*
		 * malloc() may return NULL if size is zero, so always do this to make sure
		 * that the code handles it regardless of platform.
		 */
		return NULL;
	}
	ret = malloc(size);
	if (!ret) {
		fatal("x_malloc: Could not allocate %lu bytes", (unsigned long)size);
	}
	return ret;
}

/* This is like calloc() but dies if the allocation fails. */
void *
x_calloc(size_t nmemb, size_t size)
{
	void *ret;
	if (nmemb * size == 0) {
		/*
		 * calloc() may return NULL if nmemb or size is 0, so always do this to
		 * make sure that the code handles it regardless of platform.
		 */
		return NULL;
	}
	ret = calloc(nmemb, size);
	if (!ret) {
		fatal("x_calloc: Could not allocate %lu bytes", (unsigned long)size);
	}
	return ret;
}

/*
  this is like realloc() but dies if the malloc fails
*/
void *
x_realloc(void *ptr, size_t size)
{
	void *p2;
	if (!ptr) return x_malloc(size);
	p2 = realloc(ptr, size);
	if (!p2) {
		fatal("x_realloc: Could not allocate %lu bytes", (unsigned long)size);
	}
	return p2;
}

/* This is like unsetenv. */
void x_unsetenv(const char *name)
{
#ifdef HAVE_UNSETENV
	unsetenv(name);
#else
	putenv(x_strdup(name)); /* Leak to environment. */
#endif
}

/*
 * Construct a string according to the format and store it in *ptr. The
 * original *ptr is then freed.
 */
void
reformat(char **ptr, const char *format, ...)
{
	char *saved = *ptr;
	va_list ap;

	*ptr = NULL;
	va_start(ap, format);
	if (vasprintf(ptr, format, ap) == -1) {
		fatal("Out of memory in reformat");
	}
	va_end(ap);

	if (!ptr) fatal("Out of memory in reformat");
	if (saved) {
		free(saved);
	}
}

/*
 * Recursive directory traversal. fn() is called on all entries in the tree.
 */
void
traverse(const char *dir, void (*fn)(const char *, struct stat *))
{
	DIR *d;
	struct dirent *de;

	d = opendir(dir);
	if (!d) return;

	while ((de = readdir(d))) {
		char *fname;
		struct stat st;

		if (str_eq(de->d_name, ".")) continue;
		if (str_eq(de->d_name, "..")) continue;

		if (strlen(de->d_name) == 0) continue;

		fname = format("%s/%s", dir, de->d_name);
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
char *
basename(const char *path)
{
	char *p;
	p = strrchr(path, '/');
	if (p) path = p + 1;
#ifdef _WIN32
	p = strrchr(path, '\\');
	if (p) path = p + 1;
#endif

	return x_strdup(path);
}

/* return the dir name of a file - caller frees */
char *
dirname(const char *path)
{
	char *p;
	char *p2 = NULL;
	char *s;
	s = x_strdup(path);
	p = strrchr(s, '/');
#ifdef _WIN32
	p2 = strrchr(s, '\\');
#endif
	if (p < p2)
		p = p2;
	if (p == s) {
		return s;
	} else if (p) {
		*p = 0;
		return s;
	} else {
		free(s);
		return x_strdup(".");
	}
}

/*
 * Return the file extension (including the dot) of a path as a pointer into
 * path. If path has no file extension, the empty string and the end of path is
 * returned.
 */
const char *
get_extension(const char *path)
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
char *
remove_extension(const char *path)
{
	return x_strndup(path, strlen(path) - strlen(get_extension(path)));
}

/* return size on disk of a file */
size_t
file_size(struct stat *st)
{
#ifdef _WIN32
	return (st->st_size + 1023) & ~1023;
#else
	size_t size = st->st_blocks * 512;
	if ((size_t)st->st_size > size) {
		/* probably a broken stat() call ... */
		size = (st->st_size + 1023) & ~1023;
	}
	return size;
#endif
}

/*
 * Create a file for writing. Creates parent directories if they don't exist.
 */
int
safe_create_wronly(const char *fname)
{
	int fd = open(fname, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
	if (fd == -1 && errno == ENOENT) {
		/*
		 * Only make sure parent directories exist when have failed to open the
		 * file -- this saves stat() calls.
		 */
		if (create_parent_dirs(fname) == 0) {
			fd = open(fname, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0666);
		}
	}
	return fd;
}

/* Format a size as a human-readable string. Caller frees. */
char *
format_human_readable_size(uint64_t v)
{
	char *s;
	if (v >= 1000*1000*1000) {
		s = format("%.1f GB", v/((double)(1000*1000*1000)));
	} else if (v >= 1000*1000) {
		s = format("%.1f MB", v/((double)(1000*1000)));
	} else {
		s = format("%.1f kB", v/((double)(1000)));
	}
	return s;
}

/* Format a size as a parsable string. Caller frees. */
char *
format_parsable_size_with_suffix(uint64_t size)
{
	char *s;
	if (size >= 1000*1000*1000) {
		s = format("%.1fG", size / ((double)(1000*1000*1000)));
	} else if (size >= 1000*1000) {
		s = format("%.1fM", size / ((double)(1000*1000)));
	} else if (size >= 1000) {
		s = format("%.1fk", size / ((double)(1000)));
	} else {
		s = format("%u", (unsigned)size);
	}
	return s;
}

/*
 * Parse a "size value", i.e. a string that can end in k, M, G, T (10-based
 * suffixes) or Ki, Mi, Gi, Ti (2-based suffixes). For backward compatibility,
 * K is also recognized as a synonym of k.
 */
bool
parse_size_with_suffix(const char *str, uint64_t *size)
{
	char *p;
	double x;

	errno = 0;
	x = strtod(str, &p);
	if (errno != 0 || x < 0 || p == str || *str == '\0') {
		return false;
	}

	while (isspace(*p)) {
		++p;
	}

	if (*p != '\0') {
		unsigned multiplier;
		if (*(p+1) == 'i') {
			multiplier = 1024;
		} else {
			multiplier = 1000;
		}
		switch (*p) {
		case 'T':
			x *= multiplier;
		case 'G':
			x *= multiplier;
		case 'M':
			x *= multiplier;
		case 'K':
		case 'k':
			x *= multiplier;
			break;
		default:
			return false;
		}
	}
	*size = x;
	return true;
}


/*
  a sane realpath() function, trying to cope with stupid path limits and
  a broken API
*/
char *
x_realpath(const char *path)
{
	long maxlen = path_max(path);
	char *ret, *p;
#ifdef _WIN32
	HANDLE path_handle;
#endif

	ret = x_malloc(maxlen);

#if HAVE_REALPATH
	p = realpath(path, ret);
#elif defined(_WIN32)
	path_handle = CreateFile(
		path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	GetFinalPathNameByHandle(path_handle, ret, maxlen, FILE_NAME_NORMALIZED);
	CloseHandle(path_handle);
	p = ret+4;// strip the \\?\ from the file name
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
char *
gnu_getcwd(void)
{
	unsigned size = 128;

	while (true) {
		char *buffer = (char *)x_malloc(size);
		if (getcwd(buffer, size) == buffer) {
			return buffer;
		}
		free(buffer);
		if (errno != ERANGE) {
			cc_log("getcwd error: %d (%s)", errno, strerror(errno));
			return NULL;
		}
		size *= 2;
	}
}

#ifndef HAVE_STRTOK_R
/* strtok_r replacement */
char *
strtok_r(char *str, const char *delim, char **saveptr)
{
	int len;
	char *ret;
	if (!str)
		str = *saveptr;
	len = strlen(str);
	ret = strtok(str, delim);
	if (ret) {
		char *save = ret;
		while (*save++);
		if ((len + 1) == (intptr_t) (save - str))
			save--;
		*saveptr = save;
	}
	return ret;
}
#endif

/* create an empty file */
int
create_empty_file(const char *fname)
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
const char *
get_home_directory(void)
{
	const char *p = getenv("HOME");
	if (p) {
		return p;
	}
#ifdef _WIN32
	p = getenv("APPDATA");
	if (p) {
		return p;
	}
#endif
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
 * is used. Caller frees.
 */
char *
get_cwd(void)
{
	char *pwd;
	char *cwd;
	struct stat st_pwd;
	struct stat st_cwd;

	cwd = gnu_getcwd();
	if (!cwd) {
		return NULL;
	}
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
		free(cwd);
		return x_strdup(pwd);
	} else {
		return cwd;
	}
}

/*
 * Check whether s1 and s2 have the same executable name.
 */
bool
same_executable_name(const char *s1, const char *s2)
{
#ifdef _WIN32
	bool eq = strcasecmp(s1, s2) == 0;
	if (!eq) {
		char *tmp = format("%s.exe", s2);
		eq = strcasecmp(s1, tmp) == 0;
		free(tmp);
	}
	return eq;
#else
	return str_eq(s1, s2);
#endif
}

/*
 * Compute the length of the longest directory path that is common to two
 * paths. s1 is assumed to be the path to a directory.
 */
size_t
common_dir_prefix_length(const char *s1, const char *s2)
{
	const char *p1 = s1;
	const char *p2 = s2;

	while (*p1 && *p2 && *p1 == *p2) {
		++p1;
		++p2;
	}
	if (*p2 == '/') {
		/* s2 starts with "s1/". */
		return p1 - s1;
	}
	if (!*p2) {
		/* s2 is equal to s1. */
		if (p2 == s2 + 1) {
			/* Special case for s1 and s2 both being "/". */
			return 0;
		} else {
			return p1 - s1;
		}
	}
	/* Compute the common directory prefix */
	while (p1 > s1 && *p1 != '/') {
		p1--;
		p2--;
	}
	return p1 - s1;
}

/*
 * Compute a relative path from from (an absolute path to a directory) to to (a
 * path). Assumes that both from and to are well-formed and canonical. Caller
 * frees.
 */
char *
get_relative_path(const char *from, const char *to)
{
	size_t common_prefix_len;
	int i;
	const char *p;
	char *result;

	assert(from && from[0] == '/');
	assert(to);

	if (!*to || *to != '/') {
		return x_strdup(to);
	}

	result = x_strdup("");
	common_prefix_len = common_dir_prefix_length(from, to);
	if (common_prefix_len > 0 || !str_eq(from, "/")) {
		for (p = from + common_prefix_len; *p; p++) {
			if (*p == '/') {
				reformat(&result, "../%s", result);
			}
		}
	}
	if (strlen(to) > common_prefix_len) {
		reformat(&result, "%s%s", result, to + common_prefix_len + 1);
	}
	i = strlen(result) - 1;
	while (i >= 0 && result[i] == '/') {
		result[i] = '\0';
		i--;
	}
	if (str_eq(result, "")) {
		free(result);
		result = x_strdup(".");
	}
	return result;
}

/*
 * Return whether path is absolute.
 */
bool
is_absolute_path(const char *path)
{
#ifdef _WIN32
	return path[0] && path[1] == ':';
#else
	return path[0] == '/';
#endif
}

/*
 * Return whether the argument is a full path.
 */
bool
is_full_path(const char *path)
{
	if (strchr(path, '/'))
		return true;
#ifdef _WIN32
	if (strchr(path, '\\'))
		return true;
#endif
	return false;
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

/*
 * Rename oldpath to newpath (deleting newpath).
 */
int
x_rename(const char *oldpath, const char *newpath)
{
#ifdef _WIN32
	/* Windows' rename() refuses to overwrite an existing file. */
	unlink(newpath);  /* not x_unlink, as x_unlink calls x_rename */
#endif
	return rename(oldpath, newpath);
}

/*
 * Remove path, NFS hazardous. Use only for temporary files that will not exist
 * on other systems. That is, the path should include tmp_string().
 */
int
tmp_unlink(const char *path)
{
	cc_log("Unlink %s", path);
	return unlink(path);
}

/*
 * Remove path, NFS safe.
 */
int
x_unlink(const char *path)
{
	/*
	 * If path is on an NFS share, unlink isn't atomic, so we rename to a temp
	 * file. We don't care if the temp file is trashed, so it's always safe to
	 * unlink it first.
	 */
	char *tmp_name = format("%s.%s.rmXXXXXX", path, tmp_string());
	int result = 0;
	cc_log("Unlink %s via %s", path, tmp_name);
	if (x_rename(path, tmp_name) == -1) {
		result = -1;
		goto out;
	}
	if (unlink(tmp_name) == -1) {
		result = -1;
	}
out:
	free(tmp_name);
	return result;
}

#ifndef _WIN32
/* Like readlink() but returns the string or NULL on failure. Caller frees. */
char *
x_readlink(const char *path)
{
	long maxlen = path_max(path);
	ssize_t len;
	char *buf;

	buf = x_malloc(maxlen);
	len = readlink(path, buf, maxlen-1);
	if (len == -1) {
		free(buf);
		return NULL;
	}
	buf[len] = 0;
	return buf;
}
#endif

/*
 * Reads the content of a file. Size hint 0 means no hint. Returns true on
 * success, otherwise false.
 */
bool
read_file(const char *path, size_t size_hint, char **data, size_t *size)
{
	int fd, ret;
	size_t pos = 0, allocated;

	if (size_hint == 0) {
		struct stat st;
		if (stat(path, &st) == 0) {
			size_hint = st.st_size;
		}
	}
	size_hint = (size_hint < 1024) ? 1024 : size_hint;

	fd = open(path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		return false;
	}
	allocated = size_hint;
	*data = x_malloc(allocated);
	while (true) {
		if (pos > allocated / 2) {
			allocated *= 2;
			*data = x_realloc(*data, allocated);
		}
		ret = read(fd, *data + pos, allocated - pos);
		if (ret == 0 || (ret == -1 && errno != EINTR)) {
			break;
		}
		if (ret > 0) {
			pos += ret;
		}
	}
	close(fd);
	if (ret == -1) {
		cc_log("Failed reading %s", path);
		free(*data);
		*data = NULL;
		return false;
	}

	*size = pos;
	return true;
}

/*
 * Return the content (with NUL termination) of a text file, or NULL on error.
 * Caller frees. Size hint 0 means no hint.
 */
char *
read_text_file(const char *path, size_t size_hint)
{
	size_t size;
	char *data;

	if (read_file(path, size_hint, &data, &size)) {
		data = x_realloc(data, size + 1);
		data[size] = '\0';
		return data;
	} else {
		return NULL;
	}
}

static bool
expand_variable(const char **str, char **result, char **errmsg)
{
	bool curly;
	const char *p, *q;
	char *name;
	const char *value;

	assert(**str == '$');
	p = *str + 1;
	if (*p == '{') {
		curly = true;
		++p;
	} else {
		curly = false;
	}
	q = p;
	while (isalnum(*q) || *q == '_') {
		++q;
	}
	if (curly) {
		if (*q != '}') {
			*errmsg = format("syntax error: missing '}' after \"%s\"", p);
			return NULL;
		}
	}

	if (q == p) {
		/* Special case: don't consider a single $ the start of a variable. */
		reformat(result, "%s$", *result);
		return true;
	}

	name = x_strndup(p, q - p);
	value = getenv(name);
	if (!value) {
		*errmsg = format("environment variable \"%s\" not set", name);
		free(name);
		return false;
	}
	reformat(result, "%s%s", *result, value);
	if (!curly) {
		--q;
	}
	*str = q;
	free(name);
	return true;
}

/*
 * Substitute all instances of $VAR or ${VAR}, where VAR is an environment
 * variable, in a string. Caller frees. If one of the environment variables
 * doesn't exist, NULL will be returned and *errmsg will be an appropriate
 * error message (caller frees).
 */
char *
subst_env_in_string(const char *str, char **errmsg)
{
	const char *p; /* Interval start. */
	const char *q; /* Interval end. */
	char *result;

	assert(errmsg);
	*errmsg = NULL;

	result = x_strdup("");
	p = str;
	q = str;
	for (q = str; *q; ++q) {
		if (*q == '$') {
			reformat(&result, "%s%.*s", result, (int)(q - p), p);
			if (!expand_variable(&q, &result, errmsg)) {
				free(result);
				return NULL;
			}
			p = q + 1;
		}
	}
	reformat(&result, "%s%.*s", result, (int)(q - p), p);
	return result;
}
