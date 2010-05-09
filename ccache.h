#ifndef CCACHE_H
#define CCACHE_H

#define CCACHE_VERSION "3.0pre1"

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

/* statistics fields in storage order */
enum stats {
	STATS_NONE=0,
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
	STATS_NOTC,
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

void hash_start(struct mdfour *md);
void hash_delimiter(struct mdfour *md, const char* type);
void hash_string(struct mdfour *md, const char *s);
void hash_int(struct mdfour *md, int x);
int hash_fd(struct mdfour *md, int fd);
int hash_file(struct mdfour *md, const char *fname);
char *hash_result(struct mdfour *md);
void hash_result_as_bytes(struct mdfour *md, unsigned char *out);
void hash_buffer(struct mdfour *md, const void *s, size_t len);

int cc_log_no_newline(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
int cc_log(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
int cc_log_executed_command(char **argv);
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
void x_asprintf(char **ptr, const char *format, ...) ATTR_FORMAT(printf, 2, 3);
char *x_strdup(const char *s);
char *x_strndup(const char *s, size_t n);
void *x_realloc(void *ptr, size_t size);
void *x_malloc(size_t size);
void traverse(const char *dir, void (*fn)(const char *, struct stat *));
char *basename(const char *s);
char *dirname(char *s);
const char *get_extension(const char *path);
char *remove_extension(const char *path);
int read_lock_fd(int fd);
int write_lock_fd(int fd);
size_t file_size(struct stat *st);
int safe_open(const char *fname);
char *x_realpath(const char *path);
char *gnu_getcwd(void);
int create_empty_file(const char *fname);
const char *get_home_directory(void);
char *get_cwd();
size_t common_dir_prefix_length(const char *s1, const char *s2);
char *get_relative_path(const char *from, const char *to);
void update_mtime(const char *path);

void stats_update(enum stats stat);
void stats_zero(void);
void stats_summary(void);
void stats_tocache(size_t size);
void stats_read(const char *stats_file, unsigned counters[STATS_END]);
int stats_set_limits(long maxfiles, long maxsize);
size_t value_units(const char *s);
char *format_size(size_t v);
void stats_set_sizes(const char *dir, size_t num_files, size_t total_size);

int unify_hash(struct mdfour *hash, const char *fname);

#ifndef HAVE_VASPRINTF
int vasprintf(char **, const char *, va_list) ATTR_FORMAT(printf, 2, 0);
#endif
#ifndef HAVE_ASPRINTF
int asprintf(char **ptr, const char *, ...) ATTR_FORMAT(printf, 2, 3);
#endif

#ifndef HAVE_SNPRINTF
int snprintf(char *, size_t, const char *, ...) ATTR_FORMAT(printf, 3, 4);
#endif

void cleanup_dir(const char *dir, size_t maxfiles, size_t maxsize);
void cleanup_all(const char *dir);
void wipe_all(const char *dir);

int execute(char **argv,
	    const char *path_stdout,
	    const char *path_stderr);
char *find_executable(const char *name, const char *exclude_name);
void print_command(FILE *fp, char **argv);
void print_executed_command(FILE *fp, char **argv);

typedef struct {
	char **argv;
	int argc;
} ARGS;


ARGS *args_init(int , char **);
void args_add(ARGS *args, const char *s);
void args_add_prefix(ARGS *args, const char *s);
void args_pop(ARGS *args, int n);
void args_strip(ARGS *args, const char *prefix);
void args_remove_first(ARGS *args);

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

#endif /* ifndef CCACHE_H */
