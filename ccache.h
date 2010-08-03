#ifndef CCACHE_H
#define CCACHE_H

#include "config.h"
#include "mdfour.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __GNUC__
#define ATTR_FORMAT(x, y, z) __attribute__((format (x, y, z)))
#else
#define ATTR_FORMAT(x, y, z)
#endif

#ifndef MYNAME
#define MYNAME "ccache"
#endif

extern const char CCACHE_VERSION[];

/* statistics fields in storage order */
enum stats {
	STATS_NONE = 0,
	STATS_STDOUT,
	STATS_STATUS,
	STATS_ERROR,
	STATS_TOCACHE,
	STATS_PREPROCESSOR,
	STATS_COMPILER,
	STATS_MISSING,
	STATS_CACHEHIT_CPP,
	STATS_ARGS,
	STATS_LINK,
	STATS_NUMFILES,
	STATS_TOTALSIZE,
	STATS_MAXFILES,
	STATS_MAXSIZE,
	STATS_SOURCELANG,
	STATS_DEVICE,
	STATS_NOINPUT,
	STATS_MULTIPLE,
	STATS_CONFTEST,
	STATS_UNSUPPORTED,
	STATS_OUTSTDOUT,
	STATS_CACHEHIT_DIR,
	STATS_NOOUTPUT,
	STATS_EMPTYOUTPUT,
	STATS_BADEXTRAFILE,

	STATS_END
};

#define SLOPPY_INCLUDE_FILE_MTIME 1
#define SLOPPY_FILE_MACRO 2
#define SLOPPY_TIME_MACROS 4

#define str_eq(s1, s2) (strcmp((s1), (s2)) == 0)
#define str_startswith(s, p) (strncmp((s), (p), strlen((p))) == 0)

/* ------------------------------------------------------------------------- */
/* hash.c */

void hash_start(struct mdfour *md);
void hash_delimiter(struct mdfour *md, const char* type);
void hash_string(struct mdfour *md, const char *s);
void hash_int(struct mdfour *md, int x);
int hash_fd(struct mdfour *md, int fd);
int hash_file(struct mdfour *md, const char *fname);
char *hash_result(struct mdfour *md);
void hash_result_as_bytes(struct mdfour *md, unsigned char *out);
void hash_buffer(struct mdfour *md, const void *s, size_t len);

/* ------------------------------------------------------------------------- */
/* util.c */

void cc_log(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_executed_command(char **argv);
void fatal(const char *format, ...) ATTR_FORMAT(printf, 1, 2);

void copy_fd(int fd_in, int fd_out);
int copy_file(const char *src, const char *dest, int compress_dest);
int move_file(const char *src, const char *dest, int compress_dest);
int move_uncompressed_file(const char *src, const char *dest,
                           int compress_dest);
int test_if_compressed(const char *filename);

int create_dir(const char *dir);
const char *get_hostname(void);
const char *tmp_string(void);
char *format_hash_as_string(const unsigned char *hash, unsigned size);
int create_hash_dir(char **dir, const char *hash, const char *cache_dir);
int create_cachedirtag(const char *dir);
char *format(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
char *x_strdup(const char *s);
char *x_strndup(const char *s, size_t n);
void *x_realloc(void *ptr, size_t size);
void *x_malloc(size_t size);
void traverse(const char *dir, void (*fn)(const char *, struct stat *));
char *basename(const char *s);
char *dirname(char *s);
const char *get_extension(const char *path);
char *remove_extension(const char *path);
size_t file_size(struct stat *st);
int safe_open(const char *fname);
char *x_realpath(const char *path);
char *gnu_getcwd(void);
int create_empty_file(const char *fname);
const char *get_home_directory(void);
char *get_cwd();
int compare_executable_name(const char *s1, const char *s2);
size_t common_dir_prefix_length(const char *s1, const char *s2);
char *get_relative_path(const char *from, const char *to);
int is_absolute_path(const char *path);
int is_full_path(const char *path);
void update_mtime(const char *path);
void *x_fmmap(const char *fname, off_t *size, const char *errstr);
int x_munmap(void *addr, size_t length);
int x_rename(const char *oldpath, const char *newpath);
char *x_readlink(const char *path);

/* ------------------------------------------------------------------------- */
/* stats.c */

void stats_update(enum stats stat);
void stats_flush(void);
unsigned stats_get_pending(enum stats stat);
void stats_zero(void);
void stats_summary(void);
void stats_update_size(enum stats stat, size_t size, unsigned files);
void stats_get_limits(const char *dir, unsigned *maxfiles, unsigned *maxsize);
int stats_set_limits(long maxfiles, long maxsize);
size_t value_units(const char *s);
char *format_size(size_t v);
void stats_set_sizes(const char *dir, size_t num_files, size_t total_size);

/* ------------------------------------------------------------------------- */
/* unify.c */

int unify_hash(struct mdfour *hash, const char *fname);

/* ------------------------------------------------------------------------- */
/* exitfn.c */

void exitfn_init(void);
void exitfn_add_nullary(void (*function)(void));
void exitfn_add(void (*function)(void *), void *context);
void exitfn_call(void);

/* ------------------------------------------------------------------------- */
/* snprintf.c */

#ifndef HAVE_VASPRINTF
int vasprintf(char **, const char *, va_list) ATTR_FORMAT(printf, 2, 0);
#endif
#ifndef HAVE_ASPRINTF
int asprintf(char **ptr, const char *, ...) ATTR_FORMAT(printf, 2, 3);
#endif

#ifndef HAVE_SNPRINTF
int snprintf(char *, size_t, const char *, ...) ATTR_FORMAT(printf, 3, 4);
#endif

/* ------------------------------------------------------------------------- */
/* cleanup.c */

void cleanup_dir(const char *dir, size_t maxfiles, size_t maxsize);
void cleanup_all(const char *dir);
void wipe_all(const char *dir);

/* ------------------------------------------------------------------------- */
/* execute.c */

int execute(char **argv,
            const char *path_stdout,
            const char *path_stderr);
char *find_executable(const char *name, const char *exclude_name);
void print_command(FILE *fp, char **argv);
void print_executed_command(FILE *fp, char **argv);

/* ------------------------------------------------------------------------- */
/* args.c */

struct args {
	char **argv;
	int argc;
};

struct args *args_init(int, char **);
struct args *args_init_from_string(const char *);
struct args *args_copy(struct args *args);
void args_free(struct args *args);
void args_add(struct args *args, const char *s);
void args_add_prefix(struct args *args, const char *s);
void args_extend(struct args *args, struct args *to_append);
void args_pop(struct args *args, int n);
void args_strip(struct args *args, const char *prefix);
void args_remove_first(struct args *args);
char *args_to_string(struct args *args);
int args_equal(struct args *args1, struct args *args2);

/* ------------------------------------------------------------------------- */
/* lockfile.c */

int lockfile_acquire(const char *path, unsigned staleness_limit);
void lockfile_release(const char *path);

/* ------------------------------------------------------------------------- */
/* ccache.c */

int cc_process_args(struct args *orig_args, struct args **preprocessor_args,
                    struct args **compiler_args);
void cc_reset(void);

/* ------------------------------------------------------------------------- */

#if HAVE_COMPAR_FN_T
#define COMPAR_FN_T __compar_fn_t
#else
typedef int (*COMPAR_FN_T)(const void *, const void *);
#endif

/* work with silly DOS binary open */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* mkstemp() on some versions of cygwin don't handle binary files, so
   override */
#ifdef __CYGWIN__
#undef HAVE_MKSTEMP
#endif

#ifdef _WIN32
int win32execute(char *path, char **argv, int doreturn,
                 const char *path_stdout, const char *path_stderr);
#    ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#    endif
#    include <windows.h>
#    define mkdir(a,b) mkdir(a)
#    define link(src,dst) (CreateHardLink(dst,src,NULL) ? 0 : -1)
#    define lstat(a,b) stat(a,b)
#    define execv(a,b) win32execute(a,b,0,NULL,NULL)
#    define execute(a,b,c) win32execute(*(a),a,1,b,c)
#    define PATH_DELIM ";"
#    define F_RDLCK 0
#    define F_WRLCK 0
#else
#    define PATH_DELIM ":"
#endif

#endif /* ifndef CCACHE_H */
