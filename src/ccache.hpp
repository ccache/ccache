// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2019 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#pragma once

#include "system.hpp"

#include "counters.hpp"

#include "third_party/minitrace.h"

#ifdef __GNUC__
#  define ATTR_FORMAT(x, y, z)                                                 \
    __attribute__((format(ATTRIBUTE_FORMAT_PRINTF, y, z)))
#  define ATTR_NORETURN __attribute__((noreturn))
#else
#  define ATTR_FORMAT(x, y, z)
#  define ATTR_NORETURN
#endif

#ifndef MYNAME
#  define MYNAME "ccache"
#endif

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

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
  STATS_BADOUTPUTFILE = 16,
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
  STATS_CANTUSEMODULES = 32,

  STATS_END
};

enum guessed_compiler {
  GUESSED_CLANG,
  GUESSED_GCC,
  GUESSED_NVCC,
  GUESSED_PUMP,
  GUESSED_UNKNOWN
};

extern enum guessed_compiler guessed_compiler;

#define SLOPPY_INCLUDE_FILE_MTIME (1U << 0)
#define SLOPPY_INCLUDE_FILE_CTIME (1U << 1)
#define SLOPPY_FILE_MACRO (1U << 2)
#define SLOPPY_TIME_MACROS (1U << 3)
#define SLOPPY_PCH_DEFINES (1U << 4)
// Allow us to match files based on their stats (size, mtime, ctime), without
// looking at their contents.
#define SLOPPY_FILE_STAT_MATCHES (1U << 5)
// Allow us to not include any system headers in the manifest include files,
// similar to -MM versus -M for dependencies.
#define SLOPPY_SYSTEM_HEADERS (1U << 6)
// Allow us to ignore ctimes when comparing file stats, so we can fake mtimes
// if we want to (it is much harder to fake ctimes, requires changing clock)
#define SLOPPY_FILE_STAT_MATCHES_CTIME (1U << 7)
// Allow us to not include the -index-store-path option in the manifest hash.
#define SLOPPY_CLANG_INDEX_STORE (1U << 8)
// Ignore locale settings.
#define SLOPPY_LOCALE (1U << 9)
// Allow caching even if -fmodules is used.
#define SLOPPY_MODULES (1U << 10)

#define str_eq(s1, s2) (strcmp((s1), (s2)) == 0)
#define str_startswith(s, prefix)                                              \
  (strncmp((s), (prefix), strlen((prefix))) == 0)
#define str_endswith(s, suffix)                                                \
  (strlen(s) >= strlen(suffix)                                                 \
   && str_eq((s) + strlen(s) - strlen(suffix), (suffix)))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

// Buffer size for I/O operations. Should be a multiple of 4 KiB.
#define READ_BUFFER_SIZE 65536

class Config;

// ----------------------------------------------------------------------------
// args.c

struct args
{
  char** argv;
  int argc;
};

struct args* args_init(int, const char* const*);
struct args* args_init_from_string(const char*);
struct args* args_init_from_gcc_atfile(const char* filename);
struct args* args_copy(struct args* args);
void args_free(struct args* args);
void args_add(struct args* args, const char* s);
void args_add_prefix(struct args* args, const char* s);
void args_extend(struct args* args, struct args* to_append);
void args_insert(struct args* dest, int index, struct args* src, bool replace);
void args_pop(struct args* args, int n);
void args_set(struct args* args, int index, const char* value);
void args_strip(struct args* args, const char* prefix);
void args_remove_first(struct args* args);
char* args_to_string(const struct args* args);
bool args_equal(const struct args* args1, const struct args* args2);

// ----------------------------------------------------------------------------
// legacy_util.c

void cc_log(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_bulklog(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_argv(const char* prefix, char** argv);
void cc_dump_debug_log_buffer(const char* path);
void fatal(const char* format, ...) ATTR_FORMAT(printf, 1, 2) ATTR_NORETURN;
void warn(const char* format, ...) ATTR_FORMAT(printf, 1, 2);

char* get_path_in_cache(const char* name, const char* suffix);
bool copy_fd(int fd_in, int fd_out);
bool clone_file(const char* src, const char* dest, bool via_tmp_file);
bool copy_file(const char* src, const char* dest, bool via_tmp_file);
bool move_file(const char* src, const char* dest);
const char* get_hostname(void);
const char* tmp_string(void);
char* format(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void format_hex(const uint8_t* data, size_t size, char* buffer);
void reformat(char** ptr, const char* format, ...) ATTR_FORMAT(printf, 2, 3);
char* x_strdup(const char* s);
char* x_strndup(const char* s, size_t n);
void* x_malloc(size_t size);
void* x_realloc(void* ptr, size_t size);
void x_setenv(const char* name, const char* value);
void x_unsetenv(const char* name);
char* x_basename(const char* path);
char* x_dirname(const char* path);
const char* get_extension(const char* path);
char* remove_extension(const char* path);
char* format_human_readable_size(uint64_t size);
char* format_parsable_size_with_suffix(uint64_t size);
bool parse_size_with_suffix(const char* str, uint64_t* size);
char* x_realpath(const char* path);
char* gnu_getcwd(void);
#ifndef HAVE_LOCALTIME_R
struct tm* localtime_r(const time_t* timep, struct tm* result);
#endif
#ifndef HAVE_STRTOK_R
char* strtok_r(char* str, const char* delim, char** saveptr);
#endif
int create_tmp_fd(char** fname);
FILE* create_tmp_file(char** fname, const char* mode);
const char* get_home_directory(void);
char* get_cwd(void);
bool same_executable_name(const char* s1, const char* s2);
size_t common_dir_prefix_length(const char* s1, const char* s2);
char* get_relative_path(const char* from, const char* to);
bool is_absolute_path(const char* path);
bool is_full_path(const char* path);
void update_mtime(const char* path);
void x_exit(int status) ATTR_NORETURN;
int x_rename(const char* oldpath, const char* newpath);
int tmp_unlink(const char* path);
int x_unlink(const char* path);
int x_try_unlink(const char* path);
#ifndef _WIN32
char* x_readlink(const char* path);
#endif
bool read_file(const char* path, size_t size_hint, char** data, size_t* size);
char* read_text_file(const char* path, size_t size_hint);
char* subst_env_in_string(const char* str, char** errmsg);
void set_cloexec_flag(int fd);
double time_seconds(void);

// ----------------------------------------------------------------------------
// stats.c

void stats_update(enum stats stat);
void stats_flush(void);
unsigned stats_get_pending(enum stats stat);
void stats_zero(void);
void stats_summary(void);
void stats_print(void);
void stats_update_size(const char* sfile, int64_t size, int files);
void stats_get_obsolete_limits(const char* dir,
                               unsigned* maxfiles,
                               uint64_t* maxsize);
void stats_set_sizes(const char* dir, unsigned num_files, uint64_t total_size);
void stats_add_cleanup(const char* dir, unsigned count);
void stats_timestamp(time_t time, struct counters* counters);
void stats_read(const char* path, struct counters* counters);
void stats_write(const char* path, struct counters* counters);

// ----------------------------------------------------------------------------
// exitfn.c

void exitfn_init(void);
void exitfn_add_nullary(void (*function)(void));
void exitfn_add(void (*function)(void*), void* context);
void exitfn_add_last(void (*function)(void*), void* context);
void exitfn_call(void);

// ----------------------------------------------------------------------------
// execute.c

int execute(char** argv, int fd_out, int fd_err, pid_t* pid);
char* find_executable(const char* name, const char* exclude_name);
void print_command(FILE* fp, char** argv);
char* format_command(const char* const* argv);

// ----------------------------------------------------------------------------
// lockfile.c

bool lockfile_acquire(const char* path, unsigned staleness_limit);
void lockfile_release(const char* path);

// ----------------------------------------------------------------------------
// ccache.c

extern time_t time_of_compilation;
extern bool output_is_precompiled_header;
void block_signals(void);
void unblock_signals(void);
bool cc_process_args(struct args* args,
                     struct args** preprocessor_args,
                     struct args** extra_args_to_hash,
                     struct args** compiler_args);
void cc_reset(void);
bool is_precompiled_header(const char* path);

// ----------------------------------------------------------------------------

#ifdef HAVE_COMPAR_FN_T
#  define COMPAR_FN_T __compar_fn_t
#else
typedef int (*COMPAR_FN_T)(const void*, const void*);
#endif

// Work with silly DOS binary open.
#ifndef O_BINARY
#  define O_BINARY 0
#endif

#ifdef _WIN32
char* win32argvtos(char* prefix, char** argv, int* length);
char* win32getshell(char* path);
int win32execute(
  char* path, char** argv, int doreturn, int fd_stdout, int fd_stderr);
void add_exe_ext_if_no_to_fullpath(char* full_path_win_ext,
                                   size_t max_size,
                                   const char* ext,
                                   const char* path);

#  define execute(a, b, c, d) win32execute(*(a), a, 1, b, c)
#endif
