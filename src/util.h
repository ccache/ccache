#ifndef UTIL_H
#define UTIL_H

#include "system.h"

#ifdef __GNUC__
#define ATTR_FORMAT(x, y, z) __attribute__((format (x, y, z)))
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_FORMAT(x, y, z)
#define ATTR_NORETURN
#endif

void cc_log(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_bulklog(const char *format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_argv(const char *prefix, char **argv);
void cc_dump_debug_log_buffer(const char *path);
void fatal(const char *format, ...) ATTR_FORMAT(printf, 1, 2) ATTR_NORETURN;
void warn(const char *format, ...) ATTR_FORMAT(printf, 1, 2);

void copy_fd(int fd_in, int fd_out);
int copy_file(const char *src, const char *dest, int compress_level);
// Write data to a file.
int write_file(const char *data, const char *dest, size_t length);
int move_file(const char *src, const char *dest, int compress_level);
int move_uncompressed_file(const char *src, const char *dest,
                           int compress_level);
// Write data to a file descriptor.
int safe_write(int fd_out, const char *data, size_t length);

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
void x_setenv(const char *name, const char *value);
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
int x_try_unlink(const char *path);
#ifndef _WIN32
char *x_readlink(const char *path);
#endif
bool read_file(const char *path, size_t size_hint, char **data, size_t *size);
char *read_text_file(const char *path, size_t size_hint);
char *subst_env_in_string(const char *str, char **errmsg);
void set_cloexec_flag(int fd);

#endif
