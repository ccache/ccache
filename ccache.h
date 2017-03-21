#ifndef CCACHE_H
#define CCACHE_H

#include "system.h"
#include "mdfour.h"
#include "conf.h"
#include "counters.h"

#ifdef __GNUC__
#define ATTR_FORMAT(x, y, z) __attribute__((format(x, y, z)))
#define ATTR_NORETURN __attribute__((noreturn));
#else
#define ATTR_FORMAT(x, y, z)
#define ATTR_NORETURN
#endif

#ifndef MYNAME
#define MYNAME "ccache"
#endif

extern const char CCACHE_VERSION[];

// Statistics fields in storage order.
enum stats {
	STATS_NONE = 0,
	STATS_STDOUT = 1,
	STATS_STATUS = 2,
	STATS_ERROR = 3,
	STATS_TOCACHE = 4,
	STATS_PREPROCESSOR = 5,
	STATS_COMPILER = 6,
	STATS_MISSING = 7,
	STATS_CACHEHIT_CPP = 8,
	STATS_ARGS = 9,
	STATS_LINK = 10,
	STATS_NUMFILES = 11,
	STATS_TOTALSIZE = 12,
	STATS_OBSOLETE_MAXFILES = 13,
	STATS_OBSOLETE_MAXSIZE = 14,
	STATS_SOURCELANG = 15,
	STATS_DEVICE = 16,
	STATS_NOINPUT = 17,
	STATS_MULTIPLE = 18,
	STATS_CONFTEST = 19,
	STATS_UNSUPPORTED_OPTION = 20,
	STATS_OUTSTDOUT = 21,
	STATS_CACHEHIT_DIR = 22,
	STATS_NOOUTPUT = 23,
	STATS_EMPTYOUTPUT = 24,
	STATS_BADEXTRAFILE = 25,
	STATS_COMPCHECK = 26,
	STATS_CANTUSEPCH = 27,
	STATS_PREPROCESSING = 28,
	STATS_NUMCLEANUPS = 29,
	STATS_UNSUPPORTED_DIRECTIVE = 30,
	STATS_ZEROTIMESTAMP = 31,

	STATS_END
};

#define SLOPPY_INCLUDE_FILE_MTIME 1
#define SLOPPY_INCLUDE_FILE_CTIME 2
#define SLOPPY_FILE_MACRO 4
#define SLOPPY_TIME_MACROS 8
#define SLOPPY_PCH_DEFINES 16
// Allow us to match files based on their stats (size, mtime, ctime), without
// looking at their contents.
#define SLOPPY_FILE_STAT_MATCHES 32
// Allow us to not include any system headers in the manifest include files,
// similar to -MM versus -M for dependencies.
#define SLOPPY_NO_SYSTEM_HEADERS 64

#define str_eq(s1, s2) (strcmp((s1), (s2)) == 0)
#define str_startswith(s, prefix) \
	(strncmp((s), (prefix), strlen((prefix))) == 0)
#define str_endswith(s, suffix) \
	(strlen(s) >= strlen(suffix) \
	 && str_eq((s) + strlen(s) - strlen(suffix), (suffix)))

// Buffer size for I/O operations. Should be a multiple of 4 KiB.
#define READ_BUFFER_SIZE 65536

// ----------------------------------------------------------------------------
// args.c

struct args {
	char **argv;
	int argc;
};

struct args *args_init(int, char **);
struct args *args_init_from_string(const char *);
struct args *args_init_from_gcc_atfile(const char *filename);
struct args *args_copy(struct args *args);
void args_free(struct args *args);
void args_add(struct args *args, const char *s);
void args_add_prefix(struct args *args, const char *s);
void args_extend(struct args *args, struct args *to_append);
void args_insert(struct args *dest, int index, struct args *src, bool replace);
void args_pop(struct args *args, int n);
void args_set(struct args *args, int index, const char *value);
void args_strip(struct args *args, const char *prefix);
void args_remove_first(struct args *args);
char *args_to_string(struct args *args);
bool args_equal(struct args *args1, struct args *args2);

// ----------------------------------------------------------------------------
// hash.c

void hash_start(struct mdfour *md);
void hash_buffer(struct mdfour *md, const void *s, size_t len);
char *hash_result(struct mdfour *md);
void hash_result_as_bytes(struct mdfour *md, unsigned char *out);
bool hash_equal(struct mdfour *md1, struct mdfour *md2);
void hash_delimiter(struct mdfour *md, const char *type);
void hash_string(struct mdfour *md, const char *s);
void hash_string_length(struct mdfour *md, const char *s, int length);
void hash_int(struct mdfour *md, int x);
bool hash_fd(struct mdfour *md, int fd);
bool hash_file(struct mdfour *md, const char *fname);

// ----------------------------------------------------------------------------
// util.c

void cc_log(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_bulklog(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_argv(const char *prefix, char **argv);
void fatal(const char *format, ...) ATTR_FORMAT(printf, 1, 2) ATTR_NORETURN;
void warn(const char *format, ...) ATTR_FORMAT(printf, 1, 2);

void copy_fd(int fd_in, int fd_out);
int copy_file(const char *src, const char *dest, int compress_level);
int move_file(const char *src, const char *dest, int compress_level);
int move_uncompressed_file(const char *src, const char *dest,
						   int compress_level);
bool file_is_compressed(const char *filename);
int create_dir(const char *dir);
int create_parent_dirs(const char *path);
const char *get_hostname(void);
const char *tmp_string(void);
char *format_hash_as_string(const unsigned char *hash, int size);
int create_cachedirtag(const char *dir);
char *format(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void reformat(char **ptr, const char *format, ...) ATTR_FORMAT(printf, 2, 3);
char *x_strdup(const char *s);
char *x_strndup(const char *s, size_t n);
void *x_malloc(size_t size);
void *x_calloc(size_t nmemb, size_t size);
void *x_realloc(void *ptr, size_t size);
void x_unsetenv(const char *name);
int x_fstat(int fd, struct stat *buf);
int x_lstat(const char *pathname, struct stat *buf);
int x_stat(const char *pathname, struct stat *buf);
void traverse(const char *dir, void (*fn)(const char *, struct stat *));
char *basename(const char *path);
char *dirname(const char *path);
const char *get_extension(const char *path);
char *remove_extension(const char *path);
size_t file_size(struct stat *st);
char *format_human_readable_size(uint64_t size);
char *format_parsable_size_with_suffix(uint64_t size);
bool parse_size_with_suffix(const char *str, uint64_t *size);
char *x_realpath(const char *path);
char *gnu_getcwd(void);
#ifndef HAVE_STRTOK_R
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif
int create_tmp_fd(char **fname);
FILE *create_tmp_file(char **fname, const char *mode);
const char *get_home_directory(void);
char *get_cwd(void);
bool same_executable_name(const char *s1, const char *s2);
bool path_startswith(const char* s1, const char* s2);
size_t common_dir_prefix_length(const char *s1, const char *s2);
char *get_relative_path(const char *from, const char *to);
bool is_absolute_path(const char *path);
bool is_full_path(const char *path);
bool is_symlink(const char *path);
void update_mtime(const char *path);
void x_exit(int status) ATTR_NORETURN;
int x_rename(const char *oldpath, const char *newpath);
int tmp_unlink(const char *path);
int x_unlink(const char *path);
#ifndef _WIN32
char *x_readlink(const char *path);
#endif
bool read_file(const char *path, size_t size_hint, char **data, size_t *size);
char *read_text_file(const char *path, size_t size_hint);
char *subst_env_in_string(const char *str, char **errmsg);

// ----------------------------------------------------------------------------
// stats.c

void stats_update(enum stats stat);
void stats_flush(void);
unsigned stats_get_pending(enum stats stat);
void stats_zero(void);
void stats_summary(struct conf *conf);
void stats_update_size(uint64_t size, unsigned files);
void stats_get_obsolete_limits(const char *dir, unsigned *maxfiles,
							   uint64_t *maxsize);
void stats_set_sizes(const char *dir, unsigned num_files, uint64_t total_size);
void stats_add_cleanup(const char *dir, unsigned count);
void stats_timestamp(time_t time, struct counters *counters);
void stats_read(const char *path, struct counters *counters);
void stats_write(const char *path, struct counters *counters);

// ----------------------------------------------------------------------------
// unify.c

int unify_hash(struct mdfour *hash, const char *fname);

// ----------------------------------------------------------------------------
// exitfn.c

void exitfn_init(void);
void exitfn_add_nullary(void (*function)(void));
void exitfn_add(void (*function)(void *), void *context);
void exitfn_call(void);

// ----------------------------------------------------------------------------
// cleanup.c

void cleanup_dir(struct conf *conf, const char *dir);
void cleanup_all(struct conf *conf);
void wipe_all(struct conf *conf);

// ----------------------------------------------------------------------------
// execute.c

int execute(char **argv, int fd_out, int fd_err, pid_t *pid);
char *find_executable(const char *name, const char *exclude_name);
void print_command(FILE *fp, char **argv);

// ----------------------------------------------------------------------------
// lockfile.c

bool lockfile_acquire(const char *path, unsigned staleness_limit);
void lockfile_release(const char *path);

// ----------------------------------------------------------------------------
// ccache.c

extern time_t time_of_compilation;
void block_signals(void);
void unblock_signals(void);
char * make_relative_path(char *path);
bool cc_process_args(struct args *args, struct args **preprocessor_args,
					 struct args **compiler_args);
void cc_reset(void);
bool is_precompiled_header(const char *path);

// ----------------------------------------------------------------------------

#if HAVE_COMPAR_FN_T
#define COMPAR_FN_T __compar_fn_t
#else
typedef int (*COMPAR_FN_T)(const void *, const void *);
#endif

// Work with silly DOS binary open.
#ifndef O_BINARY
#define O_BINARY 0
#endif

// mkstemp() on some versions of cygwin don't handle binary files, so override.
#ifdef __CYGWIN__
#undef HAVE_MKSTEMP
#endif

#ifdef _WIN32
char *win32argvtos(char *prefix, char **argv);
char *win32getshell(char *path);
int win32execute(char *path, char **argv, int doreturn,
				 int fd_stdout, int fd_stderr);
void add_exe_ext_if_no_to_fullpath(char *full_path_win_ext, size_t max_size,
								   const char *ext, const char *path);
#    ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#    endif
#    include <windows.h>
#    define strcasecmp   _stricmp
#    define mkdir(a, b) mkdir(a)
#    define link(src, dst) (CreateHardLink(dst, src, NULL) ? 0 : -1)
#    define lstat(a, b) stat(a, b)
#    define execv(a, b) win32execute(a, b, 0, -1, -1)
#    define execute(a, b, c, d) win32execute(*(a), a, 1, b, c)
#    define DIR_DELIM_CH '/'
#    define PATH_DELIM ";"
#    define F_RDLCK 0
#    define F_WRLCK 0
#    ifdef CCACHE_CL
#       define S_ISDIR(x) (((x) & _S_IFMT) == _S_IFDIR)
#       define S_ISREG(x) (((x) & _S_IFMT) == _S_IFREG)
#    endif
#else
#    define DIR_DELIM_CH '\\'
#    define PATH_DELIM ":"
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#endif // ifndef CCACHE_H
