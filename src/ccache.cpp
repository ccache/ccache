// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "ccache.hpp"

#include "ArgsInfo.hpp"
#include "Context.hpp"
#include "Error.hpp"
#include "FormatNonstdStringView.hpp"
#include "ProgressBar.hpp"
#include "ScopeGuard.hpp"
#include "Util.hpp"
#include "args.hpp"
#include "cleanup.hpp"
#include "compopt.hpp"
#include "compress.hpp"
#include "execute.hpp"
#include "exitfn.hpp"
#include "hash.hpp"
#include "hashutil.hpp"
#include "language.hpp"
#include "legacy_globals.hpp"
#include "manifest.hpp"
#include "result.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"
#include "third_party/minitrace.h"

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#else
#  include "third_party/getopt_long.h"
#endif

#include <fstream>
#include <limits>

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

using nonstd::string_view;

static const char VERSION_TEXT[] = MYNAME
  " version %s\n"
  "\n"
  "Copyright (C) 2002-2007 Andrew Tridgell\n"
  "Copyright (C) 2009-2020 Joel Rosdahl and other contributors\n"
  "\n"
  "See <https://ccache.dev/credits.html> for a complete list of "
  "contributors.\n"
  "\n"
  "This program is free software; you can redistribute it and/or modify it "
  "under\n"
  "the terms of the GNU General Public License as published by the Free "
  "Software\n"
  "Foundation; either version 3 of the License, or (at your option) any "
  "later\n"
  "version.\n";

static const char USAGE_TEXT[] =
  "Usage:\n"
  "    " MYNAME
  " [options]\n"
  "    " MYNAME
  " compiler [compiler options]\n"
  "    compiler [compiler options]          (via symbolic link)\n"
  "\n"
  "Common options:\n"
  "    -c, --cleanup             delete old files and recalculate size "
  "counters\n"
  "                              (normally not needed as this is done\n"
  "                              automatically)\n"
  "    -C, --clear               clear the cache completely (except "
  "configuration)\n"
  "    -F, --max-files=N         set maximum number of files in cache to N "
  "(use 0\n"
  "                              for no limit)\n"
  "    -M, --max-size=SIZE       set maximum size of cache to SIZE (use 0 "
  "for no\n"
  "                              limit); available suffixes: k, M, G, T "
  "(decimal)\n"
  "                              and Ki, Mi, Gi, Ti (binary); default "
  "suffix: G\n"
  "    -X, --recompress LEVEL    recompress the cache (integer level or"
  " \"uncompressed\")\n"
  "    -x, --show-compression    show compression statistics\n"
  "    -p, --show-config         show current configuration options in\n"
  "                              human-readable format\n"
  "    -s, --show-stats          show summary of configuration and "
  "statistics\n"
  "                              counters in human-readable format\n"
  "    -z, --zero-stats          zero statistics counters\n"
  "\n"
  "    -h, --help                print this help text\n"
  "    -V, --version             print version and copyright information\n"
  "\n"
  "Options for scripting or debugging:\n"
  "        --dump-manifest=PATH  dump manifest file at PATH in text format\n"
  "    -k, --get-config=K        print the value of configuration key K\n"
  "        --hash-file=PATH      print the hash (160 bit BLAKE2b) of the "
  "file at\n"
  "                              PATH\n"
  "        --print-stats         print statistics counter IDs and "
  "corresponding\n"
  "                              values in machine-parsable format\n"
  "    -o, --set-config=K=V      set configuration item K to value V\n"
  "\n"
  "See also <https://ccache.dev>.\n";

enum fromcache_call_mode { FROMCACHE_DIRECT_MODE, FROMCACHE_CPP_MODE };

struct pending_tmp_file
{
  char* path;
  struct pending_tmp_file* next;
};

// Temporary files to remove at program exit.
// STAYS A GLOBAL
static struct pending_tmp_file* pending_tmp_files = NULL;

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
// CONSTEXPR
static const int k_tempdir_cleanup_interval = 2 * 24 * 60 * 60; // 2 days

#ifndef _WIN32
static sigset_t fatal_signal_set;
#endif

// PID of currently executing compiler that we have started, if any. 0 means no
// ongoing compilation. Not used in the _WIN32 case.
static pid_t compiler_pid = 0;

// This is a string that identifies the current "version" of the hash sum
// computed by ccache. If, for any reason, we want to force the hash sum to be
// different for the same input in a new ccache version, we can just change
// this string. A typical example would be if the format of one of the files
// stored in the cache changes in a backwards-incompatible way.
static const char HASH_PREFIX[] = "3";

static void
add_prefix(struct args* args, const char* prefix_command)
{
  if (str_eq(prefix_command, "")) {
    return;
  }

  struct args* prefix = args_init(0, NULL);
  char* e = x_strdup(prefix_command);
  char* saveptr = NULL;
  for (char* tok = strtok_r(e, " ", &saveptr); tok;
       tok = strtok_r(NULL, " ", &saveptr)) {
    char* p;

    p = find_executable(tok, MYNAME);
    if (!p) {
      fatal("%s: %s", tok, strerror(errno));
    }

    args_add(prefix, p);
    free(p);
  }
  free(e);

  cc_log("Using command-line prefix %s", prefix_command);
  for (int i = prefix->argc; i != 0; i--) {
    args_add_prefix(args, prefix->argv[i - 1]);
  }
  args_free(prefix);
}

static void failed(const Context& ctx) ATTR_NORETURN;

// Something went badly wrong - just execute the real compiler.
static void
failed(const Context& ctx)
{
  assert(orig_args);

  args_strip(orig_args, "--ccache-");
  add_prefix(orig_args, g_config.prefix_command().c_str());

  cc_log("Failed; falling back to running the real compiler");
  cc_log_argv("Executing ", orig_args->argv);
  exitfn_call();
  execv(orig_args->argv[0], orig_args->argv);
  fatal("execv of %s failed: %s", orig_args->argv[0], strerror(errno));
}

static const char*
temp_dir(const Config& cfg)
{
  static const char* path = NULL;
  if (path) {
    return path; // Memoize
  }
  path = g_config.temporary_dir().c_str();
  if (str_eq(path, "")) {
    path = format("%s/tmp", g_config.cache_dir().c_str());
  }
  return path;
}

void
block_signals(void)
{
#ifndef _WIN32
  sigprocmask(SIG_BLOCK, &fatal_signal_set, NULL);
#endif
}

void
unblock_signals(void)
{
#ifndef _WIN32
  sigset_t empty;
  sigemptyset(&empty);
  sigprocmask(SIG_SETMASK, &empty, NULL);
#endif
}

static void
add_pending_tmp_file(const char* path)
{
  block_signals();
  auto e = static_cast<pending_tmp_file*>(x_malloc(sizeof(pending_tmp_file)));
  e->path = x_strdup(path);
  e->next = pending_tmp_files;
  pending_tmp_files = e;
  unblock_signals();
}

static void
do_clean_up_pending_tmp_files()
{
  struct pending_tmp_file* p = pending_tmp_files;
  while (p) {
    // Can't call tmp_unlink here since its cc_log calls aren't signal safe.
    unlink(p->path);
    p = p->next;
    // Leak p->path and p here because clean_up_pending_tmp_files needs to be
    // signal safe.
  }
}

static void
clean_up_pending_tmp_files(void)
{
  block_signals();
  do_clean_up_pending_tmp_files();
  unblock_signals();
}

#ifndef _WIN32
static void
signal_handler(int signum)
{
  // Unregister handler for this signal so that we can send the signal to
  // ourselves at the end of the handler.
  signal(signum, SIG_DFL);

  // If ccache was killed explicitly, then bring the compiler subprocess (if
  // any) with us as well.
  if (signum == SIGTERM && compiler_pid != 0
      && waitpid(compiler_pid, NULL, WNOHANG) == 0) {
    kill(compiler_pid, signum);
  }

  do_clean_up_pending_tmp_files();

  if (compiler_pid != 0) {
    // Wait for compiler subprocess to exit before we snuff it.
    waitpid(compiler_pid, NULL, 0);
  }

  // Resend signal to ourselves to exit properly after returning from the
  // handler.
  kill(getpid(), signum);
}

static void
register_signal_handler(int signum)
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = signal_handler;
  act.sa_mask = fatal_signal_set;
#  ifdef SA_RESTART
  act.sa_flags = SA_RESTART;
#  endif
  sigaction(signum, &act, NULL);
}

static void
set_up_signal_handlers(void)
{
  sigemptyset(&fatal_signal_set);
  sigaddset(&fatal_signal_set, SIGINT);
  sigaddset(&fatal_signal_set, SIGTERM);
#  ifdef SIGHUP
  sigaddset(&fatal_signal_set, SIGHUP);
#  endif
#  ifdef SIGQUIT
  sigaddset(&fatal_signal_set, SIGQUIT);
#  endif

  register_signal_handler(SIGINT);
  register_signal_handler(SIGTERM);
#  ifdef SIGHUP
  register_signal_handler(SIGHUP);
#  endif
#  ifdef SIGQUIT
  register_signal_handler(SIGQUIT);
#  endif
}
#endif // _WIN32

static void
clean_up_internal_tempdir(const Config& cfg)
{
  time_t now = time(NULL);
  auto st = Stat::stat(g_config.cache_dir(), Stat::OnError::log);
  if (!st || st.mtime() + k_tempdir_cleanup_interval >= now) {
    // No cleanup needed.
    return;
  }

  update_mtime(g_config.cache_dir().c_str());

  DIR* dir = opendir(temp_dir(cfg));
  if (!dir) {
    return;
  }

  struct dirent* entry;
  while ((entry = readdir(dir))) {
    if (str_eq(entry->d_name, ".") || str_eq(entry->d_name, "..")) {
      continue;
    }

    char* path = format("%s/%s", temp_dir(cfg), entry->d_name);
    st = Stat::lstat(path, Stat::OnError::log);
    if (st && st.mtime() + k_tempdir_cleanup_interval < now) {
      tmp_unlink(path);
    }
    free(path);
  }

  closedir(dir);
}

static void
fclose_exitfn(void* context)
{
  fclose((FILE*)context);
}

static void
dump_debug_log_buffer_exitfn(void* context)
{
  if (!g_config.debug()) {
    return;
  }

  char* path = format("%s.ccache-log", (const char*)context);
  cc_dump_debug_log_buffer(path);
  free(path);
}

static void
init_hash_debug(struct hash* hash,
                const char* obj_path,
                char type,
                const char* section_name,
                FILE* debug_text_file)
{
  if (!g_config.debug()) {
    return;
  }

  char* path = format("%s.ccache-input-%c", obj_path, type);
  FILE* debug_binary_file = fopen(path, "wb");
  if (debug_binary_file) {
    hash_enable_debug(hash, section_name, debug_binary_file, debug_text_file);
    exitfn_add(fclose_exitfn, debug_binary_file);
  } else {
    cc_log("Failed to open %s: %s", path, strerror(errno));
  }
  free(path);
}

static enum guessed_compiler
guess_compiler(const char* path)
{
  string_view name = Util::base_name(path);
  enum guessed_compiler result = GUESSED_UNKNOWN;
  if (name == "clang") {
    result = GUESSED_CLANG;
  } else if (name == "gcc" || name == "g++") {
    result = GUESSED_GCC;
  } else if (name == "nvcc") {
    result = GUESSED_NVCC;
  } else if (name == "pump" || name == "distcc-pump") {
    result = GUESSED_PUMP;
  }
  return result;
}

static char*
get_current_working_dir(Context& ctx)
{
  if (!current_working_dir) {
    char* cwd = get_cwd();
    if (cwd) {
      current_working_dir = x_realpath(cwd);
      free(cwd);
    }
    if (!current_working_dir) {
      cc_log("Unable to determine current working directory: %s",
             strerror(errno));
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }
  }
  return current_working_dir;
}

static bool
do_remember_include_file(Context& ctx,
                         std::string path,
                         struct hash* cpp_hash,
                         bool system,
                         struct hash* depend_mode_hash)
{
  struct hash* fhash = nullptr;
  bool is_pch = false;

  if (path.length() >= 2 && path[0] == '<' && path[path.length() - 1] == '>') {
    // Typically <built-in> or <command-line>.
    return true;
  }

  if (path == ctx.args_info.input_file) {
    // Don't remember the input file.
    return true;
  }

  if (system && (g_config.sloppiness() & SLOPPY_SYSTEM_HEADERS)) {
    // Don't remember this system header.
    return true;
  }

  if (g_included_files.find(path) != g_included_files.end()) {
    // Already known include file.
    return true;
  }

#ifdef _WIN32
  {
    // stat fails on directories on win32.
    DWORD attributes = GetFileAttributes(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES
        && attributes & FILE_ATTRIBUTE_DIRECTORY) {
      return true;
    }
  }
#endif

  auto st = Stat::stat(path, Stat::OnError::log);
  if (!st) {
    return false;
  }
  if (st.is_directory()) {
    // Ignore directory, typically $PWD.
    return true;
  }
  if (!st.is_regular()) {
    // Device, pipe, socket or other strange creature.
    cc_log("Non-regular include file %s", path.c_str());
    return false;
  }

  // Canonicalize path for comparison; clang uses ./header.h.
  {
    const char* canonical = path.c_str();
    size_t canonical_len = path.length();
    if (canonical[0] == '.' && canonical[1] == '/') {
      canonical += 2;
      canonical_len -= 2;
    }

    for (size_t i = 0; i < ignore_headers_len; i++) {
      char* ignore = ignore_headers[i];
      size_t ignore_len = strlen(ignore);
      if (ignore_len > canonical_len) {
        continue;
      }
      if (strncmp(canonical, ignore, ignore_len) == 0
          && (ignore[ignore_len - 1] == DIR_DELIM_CH
              || canonical[ignore_len] == DIR_DELIM_CH
              || canonical[ignore_len] == '\0')) {
        return true;
      }
    }
  }

  // The comparison using >= is intentional, due to a possible race between
  // starting compilation and writing the include file. See also the notes
  // under "Performance" in doc/MANUAL.adoc.
  if (!(g_config.sloppiness() & SLOPPY_INCLUDE_FILE_MTIME)
      && st.mtime() >= time_of_compilation) {
    cc_log("Include file %s too new", path.c_str());
    return false;
  }

  // The same >= logic as above applies to the change time of the file.
  if (!(g_config.sloppiness() & SLOPPY_INCLUDE_FILE_CTIME)
      && st.ctime() >= time_of_compilation) {
    cc_log("Include file %s ctime too new", path.c_str());
    return false;
  }

  // Let's hash the include file content.
  std::unique_ptr<struct hash, decltype(&hash_free)> fhash_holder(hash_init(),
                                                                  &hash_free);
  fhash = fhash_holder.get();

  is_pch = is_precompiled_header(path.c_str());
  if (is_pch) {
    if (!included_pch_file) {
      cc_log("Detected use of precompiled header: %s", path.c_str());
    }
    bool using_pch_sum = false;
    if (g_config.pch_external_checksum()) {
      // hash pch.sum instead of pch when it exists
      // to prevent hashing a very large .pch file every time
      std::string pch_sum_path = fmt::format("{}.sum", path);
      if (Stat::stat(pch_sum_path, Stat::OnError::log)) {
        path = std::move(pch_sum_path);
        using_pch_sum = true;
        cc_log("Using pch.sum file %s", path.c_str());
      }
    }

    if (!hash_file(fhash, path.c_str())) {
      return false;
    }
    hash_delimiter(cpp_hash, using_pch_sum ? "pch_sum_hash" : "pch_hash");
    char pch_digest[DIGEST_STRING_BUFFER_SIZE];
    hash_result_as_string(fhash, pch_digest);
    hash_string(cpp_hash, pch_digest);
  }

  if (g_config.direct_mode()) {
    if (!is_pch) { // else: the file has already been hashed.
      char* source = NULL;
      size_t size;
      if (st.size() > 0) {
        if (!read_file(path.c_str(), st.size(), &source, &size)) {
          return false;
        }
      } else {
        source = x_strdup("");
        size = 0;
      }

      int result =
        hash_source_code_string(g_config, fhash, source, size, path.c_str());
      free(source);
      if (result & HASH_SOURCE_CODE_ERROR
          || result & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }
    }

    digest d;
    hash_result_as_bytes(fhash, &d);
    g_included_files.emplace(path, d);

    if (depend_mode_hash) {
      hash_delimiter(depend_mode_hash, "include");
      char digest[DIGEST_STRING_BUFFER_SIZE];
      digest_as_string(&d, digest);
      hash_string(depend_mode_hash, digest);
    }
  }

  return true;
}

// This function hashes an include file and stores the path and hash in the
// global g_included_files variable. If the include file is a PCH, cpp_hash is
// also updated.
static void
remember_include_file(Context& ctx,
                      const std::string& path,
                      struct hash* cpp_hash,
                      bool system,
                      struct hash* depend_mode_hash)
{
  if (!do_remember_include_file(ctx, path, cpp_hash, system, depend_mode_hash)
      && g_config.direct_mode()) {
    cc_log("Disabling direct mode");
    g_config.set_direct_mode(false);
  }
}

static void
print_included_files(Context& ctx, FILE* fp)
{
  for (const auto& item : g_included_files) {
    fprintf(fp, "%s\n", item.first.c_str());
  }
}

// Make a relative path from current working directory to path if path is under
// the base directory.
static std::string
make_relative_path(Context& ctx, const char* path)
{
  if (ctx.config.base_dir().empty()
      || !str_startswith(path, ctx.config.base_dir().c_str())) {
    return path;
  }

#ifdef _WIN32
  char* winpath = nullptr;
  if (path[0] == '/') {
    if (isalpha(path[1]) && path[2] == '/') {
      // Transform /c/path... to c:/path...
      winpath = format("%c:/%s", path[1], &path[3]);
    } else {
      winpath = x_strdup(path + 1); // Skip leading slash.
    }
    path = winpath;
  }
#endif

  char* dir = nullptr;
  char* path_suffix = nullptr;
  char* canon_path = nullptr;
  char* relpath = nullptr;

  // x_realpath only works for existing paths, so if path doesn't exist, try
  // x_dirname(path) and assemble the path afterwards. We only bother to try
  // canonicalizing one of these two paths since a compiler path argument
  // typically only makes sense if path or x_dirname(path) exists.

  if (!Stat::stat(path)) {
    // path doesn't exist.
    dir = x_dirname(path);
    // find the nearest existing directory in path
    while (!Stat::stat(dir)) {
      char* parent_dir = x_dirname(dir);
      free(dir);
      dir = parent_dir;
    }

    // suffix is the remaining of the path, skip the first delimiter
    size_t dir_len = strlen(dir);
    if (path[dir_len] == '/' || path[dir_len] == '\\') {
      dir_len++;
    }
    path_suffix = x_strdup(&path[dir_len]);
    path = dir;
  }

  std::string result;

  canon_path = x_realpath(path);
  if (canon_path) {
    relpath = get_relative_path(get_current_working_dir(ctx), canon_path);
    if (path_suffix) {
      result = fmt::format("{}/{}", relpath, path_suffix);
    } else {
      result = relpath;
    }
  } else {
    // path doesn't exist, so leave it as it is.
    result = path;
  }

#ifdef _WIN32
  free(winpath);
#endif
  free(dir);
  free(path_suffix);
  free(canon_path);
  free(relpath);

  return result;
}

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in the global variable
//   g_included_files.
static bool
process_preprocessed_file(Context& ctx,
                          struct hash* hash,
                          const char* path,
                          bool pump)
{
  char* data;
  size_t size;
  if (!read_file(path, 0, &data, &size)) {
    return false;
  }

  ignore_headers = NULL;
  ignore_headers_len = 0;
  if (!g_config.ignore_headers_in_manifest().empty()) {
    char *header, *p, *q, *saveptr = NULL;
    p = x_strdup(g_config.ignore_headers_in_manifest().c_str());
    q = p;
    while ((header = strtok_r(q, PATH_DELIM, &saveptr))) {
      ignore_headers = static_cast<char**>(
        x_realloc(ignore_headers, (ignore_headers_len + 1) * sizeof(char*)));
      ignore_headers[ignore_headers_len++] = x_strdup(header);
      q = NULL;
    }
    free(p);
  }

  char* cwd = gnu_getcwd();

  // Bytes between p and q are pending to be hashed.
  char* p = data;
  char* q = data;
  char* end = data + size;

  // There must be at least 7 characters (# 1 "x") left to potentially find an
  // include file path.
  while (q < end - 7) {
    // Check if we look at a line containing the file name of an included file.
    // At least the following formats exist (where N is a positive integer):
    //
    // GCC:
    //
    //   # N "file"
    //   # N "file" N
    //   #pragma GCC pch_preprocess "file"
    //
    // HP's compiler:
    //
    //   #line N "file"
    //
    // AIX's compiler:
    //
    //   #line N "file"
    //   #line N
    //
    // Note that there may be other lines starting with '#' left after
    // preprocessing as well, for instance "#    pragma".
    if (q[0] == '#'
        // GCC:
        && ((q[1] == ' ' && q[2] >= '0' && q[2] <= '9')
            // GCC precompiled header:
            || (q[1] == 'p'
                && str_startswith(&q[2], "ragma GCC pch_preprocess "))
            // HP/AIX:
            || (q[1] == 'l' && q[2] == 'i' && q[3] == 'n' && q[4] == 'e'
                && q[5] == ' '))
        && (q == data || q[-1] == '\n')) {
      // Workarounds for preprocessor linemarker bugs in GCC version 6.
      if (q[2] == '3') {
        if (str_startswith(q, "# 31 \"<command-line>\"\n")) {
          // Bogus extra line with #31, after the regular #1: Ignore the whole
          // line, and continue parsing.
          hash_string_buffer(hash, p, q - p);
          while (q < end && *q != '\n') {
            q++;
          }
          q++;
          p = q;
          continue;
        } else if (str_startswith(q, "# 32 \"<command-line>\" 2\n")) {
          // Bogus wrong line with #32, instead of regular #1: Replace the line
          // number with the usual one.
          hash_string_buffer(hash, p, q - p);
          q += 1;
          q[0] = '#';
          q[1] = ' ';
          q[2] = '1';
          p = q;
        }
      }

      while (q < end && *q != '"' && *q != '\n') {
        q++;
      }
      if (q < end && *q == '\n') {
        // A newline before the quotation mark -> no match.
        continue;
      }
      q++;
      if (q >= end) {
        cc_log("Failed to parse included file path");
        free(data);
        free(cwd);
        return false;
      }
      // q points to the beginning of an include file path
      hash_string_buffer(hash, p, q - p);
      p = q;
      while (q < end && *q != '"') {
        q++;
      }
      // Look for preprocessor flags, after the "filename".
      bool system = false;
      char* r = q + 1;
      while (r < end && *r != '\n') {
        if (*r == '3') { // System header.
          system = true;
        }
        r++;
      }
      // p and q span the include file path.
      char* inc_path = x_strndup(p, q - p);
      if (!has_absolute_include_headers) {
        has_absolute_include_headers = is_absolute_path(inc_path);
      }
      char* saved_inc_path = inc_path;
      inc_path = x_strdup(make_relative_path(ctx, inc_path).c_str());
      free(saved_inc_path);

      bool should_hash_inc_path = true;
      if (!g_config.hash_dir()) {
        if (str_startswith(inc_path, cwd) && str_endswith(inc_path, "//")) {
          // When compiling with -g or similar, GCC adds the absolute path to
          // CWD like this:
          //
          //   # 1 "CWD//"
          //
          // If the user has opted out of including the CWD in the hash, don't
          // hash it. See also how debug_prefix_map is handled.
          should_hash_inc_path = false;
        }
      }
      if (should_hash_inc_path) {
        hash_string_buffer(hash, inc_path, strlen(inc_path));
      }

      remember_include_file(ctx, inc_path, hash, system, NULL);
      free(inc_path);
      p = q; // Everything of interest between p and q has been hashed now.
    } else if (q[0] == '.' && q[1] == 'i' && q[2] == 'n' && q[3] == 'c'
               && q[4] == 'b' && q[5] == 'i' && q[6] == 'n') {
      // An assembler .inc bin (without the space) statement, which could be
      // part of inline assembly, refers to an external file. If the file
      // changes, the hash should change as well, but finding out what file to
      // hash is too hard for ccache, so just bail out.
      cc_log(
        "Found unsupported .inc"
        "bin directive in source code");
      stats_update(ctx, STATS_UNSUPPORTED_DIRECTIVE);
      failed(ctx);
    } else if (pump && strncmp(q, "_________", 9) == 0) {
      // Unfortunately the distcc-pump wrapper outputs standard output lines:
      // __________Using distcc-pump from /usr/bin
      // __________Using # distcc servers in pump mode
      // __________Shutting down distcc-pump include server
      while (q < end && *q != '\n') {
        q++;
      }
      if (*q == '\n') {
        q++;
      }
      p = q;
      continue;
    } else {
      q++;
    }
  }

  hash_string_buffer(hash, p, (end - p));
  free(data);
  free(cwd);

  // Explicitly check the .gch/.pch/.pth file as Clang does not include any
  // mention of it in the preprocessed output.
  if (included_pch_file) {
    std::string pch_path = make_relative_path(ctx, included_pch_file);
    hash_string(hash, pch_path);
    remember_include_file(ctx, pch_path, hash, false, nullptr);
  }

  bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
  if (debug_included) {
    print_included_files(ctx, stdout);
  }

  return true;
}

// Replace absolute paths with relative paths in the provided dependency file.
static void
use_relative_paths_in_depfile(Context& ctx, const char* depfile)
{
  if (g_config.base_dir().empty()) {
    cc_log("Base dir not set, skip using relative paths");
    return; // nothing to do
  }
  if (!has_absolute_include_headers) {
    cc_log(
      "No absolute path for included files found, skip using relative"
      " paths");
    return; // nothing to do
  }

  FILE* f;
  f = fopen(depfile, "r");
  if (!f) {
    cc_log("Cannot open dependency file: %s (%s)", depfile, strerror(errno));
    return;
  }

  char* tmp_file = format("%s.tmp", depfile);
  FILE* tmpf = create_tmp_file(&tmp_file, "w");

  bool result = false;
  char buf[10000];
  while (fgets(buf, sizeof(buf), f) && !ferror(tmpf)) {
    char* saveptr;
    char* token = strtok_r(buf, " \t", &saveptr);
    while (token) {
      char* relpath = nullptr;
      if (is_absolute_path(token)
          && str_startswith(token, g_config.base_dir().c_str())) {
        relpath = x_strdup(make_relative_path(ctx, token).c_str());
        result = true;
      } else {
        relpath = token;
      }
      if (token != buf) { // This is a dependency file.
        fputc(' ', tmpf);
      }
      fputs(relpath, tmpf);
      if (relpath != token) {
        free(relpath);
      }
      token = strtok_r(NULL, " \t", &saveptr);
    }
  }

  if (ferror(f)) {
    cc_log("Error reading dependency file: %s, skip relative path usage",
           depfile);
    result = false;
    goto out;
  }
  if (ferror(tmpf)) {
    cc_log(
      "Error writing temporary dependency file: %s, skip relative path"
      " usage",
      tmp_file);
    result = false;
    goto out;
  }

out:
  fclose(tmpf);
  fclose(f);
  if (result) {
    if (x_rename(tmp_file, depfile) != 0) {
      cc_log(
        "Error renaming dependency file: %s -> %s (%s), skip relative"
        " path usage",
        tmp_file,
        depfile,
        strerror(errno));
      result = false;
    } else {
      cc_log("Renamed dependency file: %s -> %s", tmp_file, depfile);
    }
  }
  if (!result) {
    cc_log("Removing temporary dependency file: %s", tmp_file);
    x_unlink(tmp_file);
  }
  free(tmp_file);
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static struct digest*
result_name_from_depfile(Context& ctx, const char* depfile, struct hash* hash)
{
  FILE* f = fopen(depfile, "r");
  if (!f) {
    cc_log("Cannot open dependency file %s: %s", depfile, strerror(errno));
    return NULL;
  }

  char buf[10000];
  while (fgets(buf, sizeof(buf), f) && !ferror(f)) {
    char* saveptr;
    char* token;
    for (token = strtok_r(buf, " \t\n", &saveptr); token;
         token = strtok_r(NULL, " \t\n", &saveptr)) {
      if (str_endswith(token, ":") || str_eq(token, "\\")) {
        continue;
      }
      if (!has_absolute_include_headers) {
        has_absolute_include_headers = is_absolute_path(token);
      }
      std::string path = make_relative_path(ctx, token);
      remember_include_file(ctx, path, hash, false, hash);
    }
  }

  fclose(f);

  // Explicitly check the .gch/.pch/.pth file as it may not be mentioned in the
  // dependencies output.
  if (included_pch_file) {
    std::string pch_path = make_relative_path(ctx, included_pch_file);
    hash_string(hash, pch_path);
    remember_include_file(ctx, pch_path, hash, false, nullptr);
  }

  bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
  if (debug_included) {
    print_included_files(ctx, stdout);
  }

  auto d = static_cast<digest*>(x_malloc(sizeof(digest)));
  hash_result_as_bytes(hash, d);
  return d;
}

// Send cached stderr, if any, to stderr.
static void
send_cached_stderr(const char* path_stderr)
{
  int fd_stderr = open(path_stderr, O_RDONLY | O_BINARY);
  if (fd_stderr != -1) {
    copy_fd(fd_stderr, 2);
    close(fd_stderr);
  }
}

// Create or update the manifest file.
static void
update_manifest_file(Context& ctx)
{
  if (!g_config.direct_mode() || g_config.read_only()
      || g_config.read_only_direct()) {
    return;
  }

  auto old_st = Stat::stat(manifest_path);

  // See comment in get_file_hash_index for why saving of timestamps is forced
  // for precompiled headers.
  bool save_timestamp = (g_config.sloppiness() & SLOPPY_FILE_STAT_MATCHES)
                        || output_is_precompiled_header;

  MTR_BEGIN("manifest", "manifest_put");
  cc_log("Adding result name to %s", manifest_path);
  if (!manifest_put(
        manifest_path, *cached_result_name, g_included_files, save_timestamp)) {
    cc_log("Failed to add result name to %s", manifest_path);
  } else {
    auto st = Stat::stat(manifest_path, Stat::OnError::log);
    stats_update_size(ctx,
                      manifest_stats_file,
                      st.size_on_disk() - old_st.size_on_disk(),
                      !old_st && st ? 1 : 0);
  }
  MTR_END("manifest", "manifest_put");
}

static void
update_cached_result_globals(Context& ctx, struct digest* result_name)
{
  char result_name_string[DIGEST_STRING_BUFFER_SIZE];
  digest_as_string(result_name, result_name_string);
  cached_result_name = result_name;
  cached_result_path =
    x_strdup(Util::get_path_in_cache(g_config.cache_dir(),
                                     g_config.cache_dir_levels(),
                                     result_name_string,
                                     ".result")
               .c_str());
  stats_file =
    format("%s/%c/stats", g_config.cache_dir().c_str(), result_name_string[0]);
}

static bool
create_cachedir_tag(const std::string& dir)
{
  static char const cachedir_tag[] =
    "Signature: 8a477f597d28d172789f06886806bc55\n"
    "# This file is a cache directory tag created by ccache.\n"
    "# For information about cache directory tags, see:\n"
    "#\thttp://www.brynosaurus.com/cachedir/\n";

  std::string filename = fmt::format("{}/CACHEDIR.TAG", dir);
  auto st = Stat::stat(filename);

  if (st) {
    if (st.is_regular()) {
      return true;
    }
    errno = EEXIST;
    return false;
  }

  std::ofstream f(filename);
  if (!f) {
    return false;
  }
  f << cachedir_tag;
  if (!f) {
    return false;
  }

  return true;
}

// Run the real compiler and put the result in cache.
static void
to_cache(Context& ctx,
         struct args* args,
         struct args* depend_extra_args,
         struct hash* depend_mode_hash)
{
  args_add(args, "-o");
  args_add(args, ctx.args_info.output_obj.c_str());

  if (g_config.hard_link()) {
    // Workaround for Clang bug where it overwrites an existing object file
    // when it's compiling an assembler file, see
    // <https://bugs.llvm.org/show_bug.cgi?id=39782>.
    x_unlink(ctx.args_info.output_obj.c_str());
  }

  if (generating_diagnostics) {
    args_add(args, "--serialize-diagnostics");
    args_add(args, ctx.args_info.output_dia.c_str());
  }

  // Turn off DEPENDENCIES_OUTPUT when running cc1, because otherwise it will
  // emit a line like this:
  //
  //   tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
  x_unsetenv("DEPENDENCIES_OUTPUT");
  x_unsetenv("SUNPRO_DEPENDENCIES");

  if (g_config.run_second_cpp()) {
    args_add(args, ctx.args_info.input_file.c_str());
  } else {
    args_add(args, i_tmpfile);
  }

  if (seen_split_dwarf) {
    // Remove any pre-existing .dwo file since we want to check if the compiler
    // produced one, intentionally not using x_unlink or tmp_unlink since we're
    // not interested in logging successful deletions or failures due to
    // non-existent .dwo files.
    if (unlink(ctx.args_info.output_dwo.c_str()) == -1 && errno != ENOENT) {
      cc_log("Failed to unlink %s: %s",
             ctx.args_info.output_dwo.c_str(),
             strerror(errno));
      stats_update(ctx, STATS_BADOUTPUTFILE);
    }
  }

  cc_log("Running real compiler");
  MTR_BEGIN("execute", "compiler");
  char* tmp_stdout;
  int tmp_stdout_fd;
  char* tmp_stderr;
  int tmp_stderr_fd;
  int status;
  if (!g_config.depend_mode()) {
    tmp_stdout = format("%s/tmp.stdout", temp_dir(ctx.config));
    tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
    tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx.config));
    tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
    status = execute(args->argv, tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
    args_pop(args, 3);
  } else {
    // The cached result path is not known yet, use temporary files.
    tmp_stdout = format("%s/tmp.stdout", temp_dir(ctx.config));
    tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
    tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx.config));
    tmp_stderr_fd = create_tmp_fd(&tmp_stderr);

    // Use the original arguments (including dependency options) in depend
    // mode.
    assert(orig_args);
    struct args* depend_mode_args = args_copy(orig_args);
    args_strip(depend_mode_args, "--ccache-");
    if (depend_extra_args) {
      args_extend(depend_mode_args, depend_extra_args);
    }
    add_prefix(depend_mode_args, g_config.prefix_command().c_str());

    time_of_compilation = time(NULL);
    status = execute(
      depend_mode_args->argv, tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
    args_free(depend_mode_args);
  }
  MTR_END("execute", "compiler");

  auto st = Stat::stat(tmp_stdout, Stat::OnError::log);
  if (!st) {
    // The stdout file was removed - cleanup in progress? Better bail out.
    stats_update(ctx, STATS_MISSING);
    tmp_unlink(tmp_stdout);
    tmp_unlink(tmp_stderr);
    failed(ctx);
  }

  // distcc-pump outputs lines like this:
  // __________Using # distcc servers in pump mode
  if (st.size() != 0 && guessed_compiler != GUESSED_PUMP) {
    cc_log("Compiler produced stdout");
    stats_update(ctx, STATS_STDOUT);
    tmp_unlink(tmp_stdout);
    tmp_unlink(tmp_stderr);
    failed(ctx);
  }
  tmp_unlink(tmp_stdout);

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler into tmp_stderr.
  if (cpp_stderr) {
    char* tmp_stderr2 = format("%s.2", tmp_stderr);
    if (x_rename(tmp_stderr, tmp_stderr2)) {
      cc_log("Failed to rename %s to %s: %s",
             tmp_stderr,
             tmp_stderr2,
             strerror(errno));
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }

    int fd_cpp_stderr = open(cpp_stderr, O_RDONLY | O_BINARY);
    if (fd_cpp_stderr == -1) {
      cc_log("Failed opening %s: %s", cpp_stderr, strerror(errno));
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }

    int fd_real_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
    if (fd_real_stderr == -1) {
      cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }

    int fd_result =
      open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd_result == -1) {
      cc_log("Failed opening %s: %s", tmp_stderr, strerror(errno));
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }

    copy_fd(fd_cpp_stderr, fd_result);
    copy_fd(fd_real_stderr, fd_result);
    close(fd_cpp_stderr);
    close(fd_real_stderr);
    close(fd_result);
    tmp_unlink(tmp_stderr2);
    free(tmp_stderr2);
  }

  if (status != 0) {
    cc_log("Compiler gave exit status %d", status);
    stats_update(ctx, STATS_STATUS);

    int fd = open(tmp_stderr, O_RDONLY | O_BINARY);
    if (fd != -1) {
      // We can output stderr immediately instead of rerunning the compiler.
      copy_fd(fd, 2);
      close(fd);
      tmp_unlink(tmp_stderr);

      x_exit(status);
    }

    tmp_unlink(tmp_stderr);
    failed(ctx);
  }

  if (g_config.depend_mode()) {
    struct digest* result_name = result_name_from_depfile(
      ctx, ctx.args_info.output_dep.c_str(), depend_mode_hash);
    if (!result_name) {
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }
    update_cached_result_globals(ctx, result_name);
  }

  bool produce_dep_file =
    generating_dependencies && ctx.args_info.output_dep != "/dev/null";

  if (produce_dep_file) {
    use_relative_paths_in_depfile(ctx, ctx.args_info.output_dep.c_str());
  }

  st = Stat::stat(ctx.args_info.output_obj);
  if (!st) {
    cc_log("Compiler didn't produce an object file");
    stats_update(ctx, STATS_NOOUTPUT);
    failed(ctx);
  }
  if (st.size() == 0) {
    cc_log("Compiler produced an empty object file");
    stats_update(ctx, STATS_EMPTYOUTPUT);
    failed(ctx);
  }

  st = Stat::stat(tmp_stderr, Stat::OnError::log);
  if (!st) {
    stats_update(ctx, STATS_ERROR);
    failed(ctx);
  }
  ResultFileMap result_file_map;
  if (st.size() > 0) {
    result_file_map.emplace(FileType::stderr_output, tmp_stderr);
  }
  result_file_map.emplace(FileType::object, ctx.args_info.output_obj);
  if (generating_dependencies) {
    result_file_map.emplace(FileType::dependency, ctx.args_info.output_dep);
  }
  if (generating_coverage) {
    result_file_map.emplace(FileType::coverage, ctx.args_info.output_cov);
  }
  if (generating_stackusage) {
    result_file_map.emplace(FileType::stackusage, ctx.args_info.output_su);
  }
  if (generating_diagnostics) {
    result_file_map.emplace(FileType::diagnostic, ctx.args_info.output_dia);
  }
  if (seen_split_dwarf && Stat::stat(ctx.args_info.output_dwo)) {
    // Only copy .dwo file if it was created by the compiler (GCC and Clang
    // behave differently e.g. for "-gsplit-dwarf -g1").
    result_file_map.emplace(FileType::dwarf_object, ctx.args_info.output_dwo);
  }

  auto orig_dest_stat = Stat::stat(cached_result_path);
  result_put(ctx, cached_result_path, result_file_map);

  cc_log("Stored in cache: %s", cached_result_path);

  auto new_dest_stat = Stat::stat(cached_result_path, Stat::OnError::log);
  if (!new_dest_stat) {
    stats_update(ctx, STATS_ERROR);
    failed(ctx);
  }
  stats_update_size(ctx,
                    stats_file,
                    new_dest_stat.size_on_disk()
                      - orig_dest_stat.size_on_disk(),
                    orig_dest_stat ? 0 : 1);

  MTR_END("file", "file_put");

  stats_update(ctx, STATS_CACHEMISS);

  // Make sure we have a CACHEDIR.TAG in the cache part of cache_dir. This can
  // be done almost anywhere, but we might as well do it near the end as we
  // save the stat call if we exit early.
  {
    char* first_level_dir = x_dirname(stats_file);
    if (!create_cachedir_tag(first_level_dir) != 0) {
      cc_log("Failed to create %s/CACHEDIR.TAG (%s)",
             first_level_dir,
             strerror(errno));
    }
    free(first_level_dir);

    // Remove any CACHEDIR.TAG on the cache_dir level where it was located in
    // previous ccache versions.
    if (getpid() % 1000 == 0) {
      char* path = format("%s/CACHEDIR.TAG", g_config.cache_dir().c_str());
      x_unlink(path);
      free(path);
    }
  }

  // Everything OK.
  send_cached_stderr(tmp_stderr);
  tmp_unlink(tmp_stderr);

  update_manifest_file(ctx);

  free(tmp_stderr);
  free(tmp_stdout);
}

// Find the result name by running the compiler in preprocessor mode and
// hashing the result.
static struct digest*
get_result_name_from_cpp(Context& ctx, struct args* args, struct hash* hash)
{
  time_of_compilation = time(NULL);

  char* path_stderr = NULL;
  char* path_stdout = nullptr;
  int status;
  if (direct_i_file) {
    // We are compiling a .i or .ii file - that means we can skip the cpp stage
    // and directly form the correct i_tmpfile.
    path_stdout = x_strdup(ctx.args_info.input_file.c_str());
    status = 0;
  } else {
    // Run cpp on the input file to obtain the .i.

    // Limit the basename to 10 characters in order to cope with filesystem with
    // small maximum filename length limits.
    string_view input_base =
      Util::get_truncated_base_name(ctx.args_info.input_file, 10);
    path_stdout = x_strdup(
      fmt::format("{}/{}.stdout", temp_dir(ctx.config), input_base).c_str());
    int path_stdout_fd = create_tmp_fd(&path_stdout);
    add_pending_tmp_file(path_stdout);

    path_stderr = format("%s/tmp.cpp_stderr", temp_dir(ctx.config));
    int path_stderr_fd = create_tmp_fd(&path_stderr);
    add_pending_tmp_file(path_stderr);

    int args_added = 2;
    args_add(args, "-E");
    if (g_config.keep_comments_cpp()) {
      args_add(args, "-C");
      args_added = 3;
    }
    args_add(args, ctx.args_info.input_file.c_str());
    add_prefix(args, g_config.prefix_command_cpp().c_str());
    cc_log("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    status = execute(args->argv, path_stdout_fd, path_stderr_fd, &compiler_pid);
    MTR_END("execute", "preprocessor");
    args_pop(args, args_added);
  }

  if (status != 0) {
    cc_log("Preprocessor gave exit status %d", status);
    stats_update(ctx, STATS_PREPROCESSOR);
    failed(ctx);
  }

  hash_delimiter(hash, "cpp");
  if (!process_preprocessed_file(
        ctx, hash, path_stdout, guessed_compiler == GUESSED_PUMP)) {
    stats_update(ctx, STATS_ERROR);
    failed(ctx);
  }

  hash_delimiter(hash, "cppstderr");
  if (!direct_i_file && !hash_file(hash, path_stderr)) {
    // Somebody removed the temporary file?
    stats_update(ctx, STATS_ERROR);
    cc_log("Failed to open %s: %s", path_stderr, strerror(errno));
    failed(ctx);
  }

  if (direct_i_file) {
    i_tmpfile = x_strdup(ctx.args_info.input_file.c_str());
  } else {
    // i_tmpfile needs the proper cpp_extension for the compiler to do its
    // thing correctly
    i_tmpfile = format("%s.%s", path_stdout, g_config.cpp_extension().c_str());
    x_rename(path_stdout, i_tmpfile);
    add_pending_tmp_file(i_tmpfile);
  }

  if (g_config.run_second_cpp()) {
    free(path_stderr);
  } else {
    // If we are using the CPP trick, we need to remember this stderr data and
    // output it just before the main stderr from the compiler pass.
    cpp_stderr = path_stderr;
    hash_delimiter(hash, "runsecondcpp");
    hash_string(hash, "false");
  }

  auto name = static_cast<digest*>(x_malloc(sizeof(digest)));
  hash_result_as_bytes(hash, name);
  return name;
}

// Hash mtime or content of a file, or the output of a command, according to
// the CCACHE_COMPILERCHECK setting.
static void
hash_compiler(const Context& ctx,
              struct hash* hash,
              const Stat& st,
              const char* path,
              bool allow_command)
{
  if (g_config.compiler_check() == "none") {
    // Do nothing.
  } else if (g_config.compiler_check() == "mtime") {
    hash_delimiter(hash, "cc_mtime");
    hash_int(hash, st.size());
    hash_int(hash, st.mtime());
  } else if (Util::starts_with(g_config.compiler_check(), "string:")) {
    hash_delimiter(hash, "cc_hash");
    hash_string(hash, g_config.compiler_check().c_str() + strlen("string:"));
  } else if (g_config.compiler_check() == "content" || !allow_command) {
    hash_delimiter(hash, "cc_content");
    hash_file(hash, path);
  } else { // command string
    bool ok = hash_multicommand_output(
      ctx, hash, g_config.compiler_check().c_str(), orig_args->argv[0]);
    if (!ok) {
      fatal("Failure running compiler check command: %s",
            g_config.compiler_check().c_str());
    }
  }
}

// Hash the host compiler(s) invoked by nvcc.
//
// If ccbin_st and ccbin are set, they refer to a directory or compiler set
// with -ccbin/--compiler-bindir. If they are NULL, the compilers are looked up
// in PATH instead.
static void
hash_nvcc_host_compiler(const Context& ctx,
                        struct hash* hash,
                        const Stat* ccbin_st,
                        const char* ccbin)
{
  // From <http://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html>:
  //
  //   "[...] Specify the directory in which the compiler executable resides.
  //   The host compiler executable name can be also specified to ensure that
  //   the correct host compiler is selected."
  //
  // and
  //
  //   "On all platforms, the default host compiler executable (gcc and g++ on
  //   Linux, clang and clang++ on Mac OS X, and cl.exe on Windows) found in
  //   the current execution search path will be used".

  if (!ccbin || ccbin_st->is_directory()) {
#if defined(__APPLE__)
    const char* compilers[] = {"clang", "clang++"};
#elif defined(_WIN32)
    const char* compilers[] = {"cl.exe"};
#else
    const char* compilers[] = {"gcc", "g++"};
#endif
    for (size_t i = 0; i < ARRAY_SIZE(compilers); i++) {
      if (ccbin) {
        char* path = format("%s/%s", ccbin, compilers[i]);
        auto st = Stat::stat(path);
        if (st) {
          hash_compiler(ctx, hash, st, path, false);
        }
        free(path);
      } else {
        char* path = find_executable(compilers[i], MYNAME);
        if (path) {
          auto st = Stat::stat(path, Stat::OnError::log);
          hash_compiler(ctx, hash, st, ccbin, false);
          free(path);
        }
      }
    }
  } else {
    hash_compiler(ctx, hash, *ccbin_st, ccbin, false);
  }
}

// Update a hash with information common for the direct and preprocessor modes.
static void
hash_common_info(Context& ctx, struct args* args, struct hash* hash)
{
  hash_string(hash, HASH_PREFIX);

  // We have to hash the extension, as a .i file isn't treated the same by the
  // compiler as a .ii file.
  hash_delimiter(hash, "ext");
  hash_string(hash, g_config.cpp_extension().c_str());

#ifdef _WIN32
  const char* ext = strrchr(args->argv[0], '.');
  char full_path_win_ext[MAX_PATH + 1] = {0};
  add_exe_ext_if_no_to_fullpath(
    full_path_win_ext, MAX_PATH, ext, args->argv[0]);
  const char* full_path = full_path_win_ext;
#else
  const char* full_path = args->argv[0];
#endif

  auto st = Stat::stat(full_path, Stat::OnError::log);
  if (!st) {
    stats_update(ctx, STATS_COMPILER);
    failed(ctx);
  }

  // Hash information about the compiler.
  hash_compiler(ctx, hash, st, args->argv[0], true);

  // Also hash the compiler name as some compilers use hard links and behave
  // differently depending on the real name.
  hash_delimiter(hash, "cc_name");
  string_view base = Util::base_name(args->argv[0]);
  hash_string_view(hash, base);

  if (!(g_config.sloppiness() & SLOPPY_LOCALE)) {
    // Hash environment variables that may affect localization of compiler
    // warning messages.
    const char* envvars[] = {"LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", NULL};
    for (const char** p = envvars; *p; ++p) {
      char* v = getenv(*p);
      if (v) {
        hash_delimiter(hash, *p);
        hash_string(hash, v);
      }
    }
  }

  // Possibly hash the current working directory.
  if (ctx.args_info.generating_debuginfo && g_config.hash_dir()) {
    char* cwd = gnu_getcwd();
    for (size_t i = 0; i < ctx.args_info.debug_prefix_maps_len; i++) {
      char* map = ctx.args_info.debug_prefix_maps[i];
      char* sep = strchr(map, '=');
      if (sep) {
        char* old_path = x_strndup(map, sep - map);
        char* new_path = static_cast<char*>(x_strdup(sep + 1));
        cc_log(
          "Relocating debuginfo CWD %s from %s to %s", cwd, old_path, new_path);
        if (str_startswith(cwd, old_path)) {
          char* dir = format("%s%s", new_path, cwd + strlen(old_path));
          free(cwd);
          cwd = dir;
        }
        free(old_path);
        free(new_path);
      }
    }
    if (cwd) {
      cc_log("Hashing CWD %s", cwd);
      hash_delimiter(hash, "cwd");
      hash_string(hash, cwd);
      free(cwd);
    }
  }

  if (generating_dependencies || seen_split_dwarf) {
    // The output object file name is part of the .d file, so include the path
    // in the hash if generating dependencies.
    //
    // Object files include a link to the corresponding .dwo file based on the
    // target object filename when using -gsplit-dwarf, so hashing the object
    // file path will do it, although just hashing the object file base name
    // would be enough.
    hash_delimiter(hash, "object file");
    hash_string_view(hash, ctx.args_info.output_obj);
  }

  // Possibly hash the coverage data file path.
  if (generating_coverage && profile_arcs) {
    char* dir = x_dirname(ctx.args_info.output_obj.c_str());
    if (profile_dir) {
      dir = x_strdup(profile_dir);
    } else {
      char* real_dir = x_realpath(dir);
      free(dir);
      dir = real_dir;
    }
    if (dir) {
      string_view base_name = Util::base_name(ctx.args_info.output_obj);
      string_view p = Util::remove_extension(base_name);
      std::string gcda_path = fmt::format("{}/{}.gcda", dir, p);
      cc_log("Hashing coverage path %s", gcda_path.c_str());
      hash_delimiter(hash, "gcda");
      hash_string(hash, gcda_path);
      free(dir);
    }
  }

  // Possibly hash the sanitize blacklist file path.
  for (size_t i = 0; i < ctx.args_info.sanitize_blacklists_len; i++) {
    char* sanitize_blacklist = ctx.args_info.sanitize_blacklists[i];
    cc_log("Hashing sanitize blacklist %s", sanitize_blacklist);
    hash_delimiter(hash, "sanitizeblacklist");
    if (!hash_file(hash, sanitize_blacklist)) {
      stats_update(ctx, STATS_BADEXTRAFILE);
      failed(ctx);
    }
  }

  if (!g_config.extra_files_to_hash().empty()) {
    char* p = x_strdup(g_config.extra_files_to_hash().c_str());
    char* q = p;
    char* path;
    char* saveptr = NULL;
    while ((path = strtok_r(q, PATH_DELIM, &saveptr))) {
      cc_log("Hashing extra file %s", path);
      hash_delimiter(hash, "extrafile");
      if (!hash_file(hash, path)) {
        stats_update(ctx, STATS_BADEXTRAFILE);
        failed(ctx);
      }
      q = NULL;
    }
    free(p);
  }

  // Possibly hash GCC_COLORS (for color diagnostics).
  if (guessed_compiler == GUESSED_GCC) {
    const char* gcc_colors = getenv("GCC_COLORS");
    if (gcc_colors) {
      hash_delimiter(hash, "gcccolors");
      hash_string(hash, gcc_colors);
    }
  }
}

// Update a hash sum with information specific to the direct and preprocessor
// modes and calculate the result name. Returns the result name on success,
// otherwise NULL. Caller frees.
static struct digest*
calculate_result_name(Context& ctx,
                      struct args* args,
                      struct args* preprocessor_args,
                      struct hash* hash,
                      bool direct_mode)
{
  bool found_ccbin = false;

  hash_delimiter(hash, "result version");
  hash_int(hash, k_result_version);

  if (direct_mode) {
    hash_delimiter(hash, "manifest version");
    hash_int(hash, k_manifest_version);
  }

  // clang will emit warnings for unused linker flags, so we shouldn't skip
  // those arguments.
  int is_clang =
    guessed_compiler == GUESSED_CLANG || guessed_compiler == GUESSED_UNKNOWN;

  // First the arguments.
  for (int i = 1; i < args->argc; i++) {
    // -L doesn't affect compilation (except for clang).
    if (i < args->argc - 1 && str_eq(args->argv[i], "-L") && !is_clang) {
      i++;
      continue;
    }
    if (str_startswith(args->argv[i], "-L") && !is_clang) {
      continue;
    }

    // -Wl,... doesn't affect compilation (except for clang).
    if (str_startswith(args->argv[i], "-Wl,") && !is_clang) {
      continue;
    }

    // The -fdebug-prefix-map option may be used in combination with
    // CCACHE_BASEDIR to reuse results across different directories. Skip using
    // the value of the option from hashing but still hash the existence of the
    // option.
    if (str_startswith(args->argv[i], "-fdebug-prefix-map=")) {
      hash_delimiter(hash, "arg");
      hash_string(hash, "-fdebug-prefix-map=");
      continue;
    }
    if (str_startswith(args->argv[i], "-ffile-prefix-map=")) {
      hash_delimiter(hash, "arg");
      hash_string(hash, "-ffile-prefix-map=");
      continue;
    }
    if (str_startswith(args->argv[i], "-fmacro-prefix-map=")) {
      hash_delimiter(hash, "arg");
      hash_string(hash, "-fmacro-prefix-map=");
      continue;
    }

    // When using the preprocessor, some arguments don't contribute to the
    // hash. The theory is that these arguments will change the output of -E if
    // they are going to have any effect at all. For precompiled headers this
    // might not be the case.
    if (!direct_mode && !output_is_precompiled_header
        && !using_precompiled_header) {
      if (compopt_affects_cpp(args->argv[i])) {
        if (compopt_takes_arg(args->argv[i])) {
          i++;
        }
        continue;
      }
      if (compopt_short(compopt_affects_cpp, args->argv[i])) {
        continue;
      }
    }

    // If we're generating dependencies, we make sure to skip the filename of
    // the dependency file, since it doesn't impact the output.
    if (generating_dependencies) {
      if (str_startswith(args->argv[i], "-Wp,")) {
        if (str_startswith(args->argv[i], "-Wp,-MD,")
            && !strchr(args->argv[i] + 8, ',')) {
          hash_string_buffer(hash, args->argv[i], 8);
          continue;
        } else if (str_startswith(args->argv[i], "-Wp,-MMD,")
                   && !strchr(args->argv[i] + 9, ',')) {
          hash_string_buffer(hash, args->argv[i], 9);
          continue;
        }
      } else if (str_startswith(args->argv[i], "-MF")) {
        // In either case, hash the "-MF" part.
        hash_delimiter(hash, "arg");
        hash_string_buffer(hash, args->argv[i], 3);

        if (ctx.args_info.output_dep != "/dev/null") {
          bool separate_argument = (strlen(args->argv[i]) == 3);
          if (separate_argument) {
            // Next argument is dependency name, so skip it.
            i++;
          }
        }
        continue;
      }
    }

    char* p = NULL;
    if (str_startswith(args->argv[i], "-specs=")) {
      p = args->argv[i] + 7;
    } else if (str_startswith(args->argv[i], "--specs=")) {
      p = args->argv[i] + 8;
    }

    if (p) {
      auto st = Stat::stat(p, Stat::OnError::log);
      if (st) {
        // If given an explicit specs file, then hash that file, but don't
        // include the path to it in the hash.
        hash_delimiter(hash, "specs");
        hash_compiler(ctx, hash, st, p, false);
        continue;
      }
    }

    if (str_startswith(args->argv[i], "-fplugin=")) {
      auto st = Stat::stat(args->argv[i] + 9, Stat::OnError::log);
      if (st) {
        hash_delimiter(hash, "plugin");
        hash_compiler(ctx, hash, st, args->argv[i] + 9, false);
        continue;
      }
    }

    if (str_eq(args->argv[i], "-Xclang") && i + 3 < args->argc
        && str_eq(args->argv[i + 1], "-load")
        && str_eq(args->argv[i + 2], "-Xclang")) {
      auto st = Stat::stat(args->argv[i + 3], Stat::OnError::log);
      if (st) {
        hash_delimiter(hash, "plugin");
        hash_compiler(ctx, hash, st, args->argv[i + 3], false);
        i += 3;
        continue;
      }
    }

    if ((str_eq(args->argv[i], "-ccbin")
         || str_eq(args->argv[i], "--compiler-bindir"))
        && i + 1 < args->argc) {
      auto st = Stat::stat(args->argv[i + 1], Stat::OnError::log);
      if (st) {
        found_ccbin = true;
        hash_delimiter(hash, "ccbin");
        hash_nvcc_host_compiler(ctx, hash, &st, args->argv[i + 1]);
        i++;
        continue;
      }
    }

    // All other arguments are included in the hash.
    hash_delimiter(hash, "arg");
    hash_string(hash, args->argv[i]);
    if (i + 1 < args->argc && compopt_takes_arg(args->argv[i])) {
      i++;
      hash_delimiter(hash, "arg");
      hash_string(hash, args->argv[i]);
    }
  }

  // Make results with dependency file /dev/null different from those without
  // it.
  if (generating_dependencies && ctx.args_info.output_dep == "/dev/null") {
    hash_delimiter(hash, "/dev/null dependency file");
  }

  if (!found_ccbin && str_eq(actual_language, "cu")) {
    hash_nvcc_host_compiler(ctx, hash, NULL, NULL);
  }

  // For profile generation (-fprofile-arcs, -fprofile-generate):
  // - hash profile directory
  //
  // For profile usage (-fprofile-use):
  // - hash profile data
  //
  // -fbranch-probabilities and -fvpt usage is covered by
  // -fprofile-generate/-fprofile-use.
  //
  // The profile directory can be specified as an argument to
  // -fprofile-generate=, -fprofile-use= or -fprofile-dir=.
  if (profile_generate) {
    if (!profile_dir) {
      profile_dir = get_cwd();
    }
    cc_log("Adding profile directory %s to our hash", profile_dir);
    hash_delimiter(hash, "-fprofile-dir");
    hash_string(hash, profile_dir);
  }

  if (profile_use) {
    // Calculate gcda name.
    if (!profile_dir) {
      profile_dir = get_cwd();
    }
    string_view base_name = Util::remove_extension(ctx.args_info.output_obj);
    std::string gcda_name = fmt::format("{}/{}.gcda", profile_dir, base_name);
    cc_log("Adding profile data %s to our hash", gcda_name.c_str());
    // Add the gcda to our hash.
    hash_delimiter(hash, "-fprofile-use");
    hash_file(hash, gcda_name.c_str());
  }

  // Adding -arch to hash since cpp output is affected.
  for (size_t i = 0; i < arch_args_size; ++i) {
    hash_delimiter(hash, "-arch");
    hash_string(hash, arch_args[i]);
  }

  struct digest* result_name = NULL;
  if (direct_mode) {
    // Hash environment variables that affect the preprocessor output.
    const char* envvars[] = {"CPATH",
                             "C_INCLUDE_PATH",
                             "CPLUS_INCLUDE_PATH",
                             "OBJC_INCLUDE_PATH",
                             "OBJCPLUS_INCLUDE_PATH", // clang
                             NULL};
    for (const char** p = envvars; *p; ++p) {
      char* v = getenv(*p);
      if (v) {
        hash_delimiter(hash, *p);
        hash_string(hash, v);
      }
    }

    // Make sure that the direct mode hash is unique for the input file path.
    // If this would not be the case:
    //
    // * An false cache hit may be produced. Scenario:
    //   - a/r.h exists.
    //   - a/x.c has #include "r.h".
    //   - b/x.c is identical to a/x.c.
    //   - Compiling a/x.c records a/r.h in the manifest.
    //   - Compiling b/x.c results in a false cache hit since a/x.c and b/x.c
    //     share manifests and a/r.h exists.
    // * The expansion of __FILE__ may be incorrect.
    hash_delimiter(hash, "inputfile");
    hash_string(hash, ctx.args_info.input_file);

    hash_delimiter(hash, "sourcecode");
    int result =
      hash_source_code_file(g_config, hash, ctx.args_info.input_file.c_str());
    if (result & HASH_SOURCE_CODE_ERROR) {
      stats_update(ctx, STATS_ERROR);
      failed(ctx);
    }
    if (result & HASH_SOURCE_CODE_FOUND_TIME) {
      cc_log("Disabling direct mode");
      g_config.set_direct_mode(false);
      return NULL;
    }

    char manifest_name_string[DIGEST_STRING_BUFFER_SIZE];
    hash_result_as_string(hash, manifest_name_string);
    manifest_path =
      x_strdup(Util::get_path_in_cache(g_config.cache_dir(),
                                       g_config.cache_dir_levels(),
                                       manifest_name_string,
                                       ".manifest")
                 .c_str());
    manifest_stats_file = format(
      "%s/%c/stats", g_config.cache_dir().c_str(), manifest_name_string[0]);

    cc_log("Looking for result name in %s", manifest_path);
    MTR_BEGIN("manifest", "manifest_get");
    result_name = manifest_get(ctx, manifest_path);
    MTR_END("manifest", "manifest_get");
    if (result_name) {
      cc_log("Got result name from manifest");
    } else {
      cc_log("Did not find result name in manifest");
    }
  } else {
    assert(preprocessor_args);
    if (arch_args_size == 0) {
      result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
      cc_log("Got result name from preprocessor");
    } else {
      args_add(preprocessor_args, "-arch");
      for (size_t i = 0; i < arch_args_size; ++i) {
        args_add(preprocessor_args, arch_args[i]);
        result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
        cc_log("Got result name from preprocessor with -arch %s", arch_args[i]);
        if (i != arch_args_size - 1) {
          free(result_name);
          result_name = NULL;
        }
        args_pop(preprocessor_args, 1);
      }
      args_pop(preprocessor_args, 1);
    }
  }

  return result_name;
}

// Try to return the compile result from cache. If we can return from cache
// then this function exits with the correct status code, otherwise it returns.
static void
from_cache(Context& ctx,
           enum fromcache_call_mode mode,
           bool put_result_in_manifest)
{
  // The user might be disabling cache hits.
  if (g_config.recache()) {
    return;
  }

  // If we're using Clang, we can't trust a precompiled header object based on
  // running the preprocessor since clang will produce a fatal error when the
  // precompiled header is used and one of the included files has an updated
  // timestamp:
  //
  //     file 'foo.h' has been modified since the precompiled header 'foo.pch'
  //     was built
  if ((guessed_compiler == GUESSED_CLANG || guessed_compiler == GUESSED_UNKNOWN)
      && output_is_precompiled_header && mode == FROMCACHE_CPP_MODE) {
    cc_log("Not considering cached precompiled header in preprocessor mode");
    return;
  }

  MTR_BEGIN("cache", "from_cache");

  bool produce_dep_file =
    generating_dependencies && ctx.args_info.output_dep != "/dev/null";

  MTR_BEGIN("file", "file_get");

  // Get result from cache.
  char* tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx.config));
  int tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
  close(tmp_stderr_fd);

  ResultFileMap result_file_map;
  if (ctx.args_info.output_obj != "/dev/null") {
    result_file_map.emplace(FileType::object, ctx.args_info.output_obj);
    if (seen_split_dwarf) {
      result_file_map.emplace(FileType::dwarf_object, ctx.args_info.output_dwo);
    }
  }
  result_file_map.emplace(FileType::stderr_output, tmp_stderr);
  if (produce_dep_file) {
    result_file_map.emplace(FileType::dependency, ctx.args_info.output_dep);
  }
  if (generating_coverage) {
    result_file_map.emplace(FileType::coverage, ctx.args_info.output_cov);
  }
  if (generating_stackusage) {
    result_file_map.emplace(FileType::stackusage, ctx.args_info.output_su);
  }
  if (generating_diagnostics) {
    result_file_map.emplace(FileType::diagnostic, ctx.args_info.output_dia);
  }
  bool ok = result_get(cached_result_path, result_file_map);
  if (!ok) {
    cc_log("Failed to get result from cache");
    tmp_unlink(tmp_stderr);
    free(tmp_stderr);
    return;
  }

  MTR_END("file", "file_get");

  send_cached_stderr(tmp_stderr);

  if (put_result_in_manifest) {
    update_manifest_file(ctx);
  }

  tmp_unlink(tmp_stderr);
  free(tmp_stderr);

  // Log the cache hit.
  switch (mode) {
  case FROMCACHE_DIRECT_MODE:
    cc_log("Succeeded getting cached result");
    stats_update(ctx, STATS_CACHEHIT_DIR);
    break;

  case FROMCACHE_CPP_MODE:
    cc_log("Succeeded getting cached result");
    stats_update(ctx, STATS_CACHEHIT_CPP);
    break;
  }

  MTR_END("cache", "from_cache");

  // And exit with the right status code.
  x_exit(0);
}

// Find the real compiler. We just search the PATH to find an executable of the
// same name that isn't a link to ourselves.
static void
find_compiler(Context& ctx, char** argv)
{
  // We might be being invoked like "ccache gcc -c foo.c".
  std::string base(Util::base_name(argv[0]));
  if (same_executable_name(base.c_str(), MYNAME)) {
    args_remove_first(orig_args);
    if (is_full_path(orig_args->argv[0])) {
      // A full path was given.
      return;
    }
    base = std::string(Util::base_name(orig_args->argv[0]));
  }

  // Support user override of the compiler.
  if (!g_config.compiler().empty()) {
    base = g_config.compiler();
  }

  char* compiler = find_executable(base.c_str(), MYNAME);
  if (!compiler) {
    stats_update(ctx, STATS_COMPILER);
    fatal("Could not find compiler \"%s\" in PATH", base.c_str());
  }
  if (str_eq(compiler, argv[0])) {
    fatal("Recursive invocation (the name of the ccache binary must be \"%s\")",
          MYNAME);
  }
  orig_args->argv[0] = compiler;
}

bool
is_precompiled_header(const char* path)
{
  const char* ext = get_extension(path);
  char* dir = x_dirname(path);
  const char* dir_ext = get_extension(dir);
  bool result =
    str_eq(ext, ".gch") || str_eq(ext, ".pch") || str_eq(ext, ".pth")
    || str_eq(dir_ext, ".gch"); // See "Precompiled Headers" in GCC docs.
  free(dir);
  return result;
}

static bool
color_output_possible()
{
  const char* term_env = getenv("TERM");
  return isatty(STDERR_FILENO) && term_env && strcasecmp(term_env, "DUMB") != 0;
}

static bool
detect_pch(Context& ctx, const char* option, const char* arg, bool* found_pch)
{
  // Try to be smart about detecting precompiled headers.
  char* pch_file = NULL;
  if (str_eq(option, "-include-pch") || str_eq(option, "-include-pth")) {
    if (Stat::stat(arg)) {
      cc_log("Detected use of precompiled header: %s", arg);
      pch_file = x_strdup(arg);
    }
  } else {
    char* gchpath = format("%s.gch", arg);
    if (Stat::stat(gchpath)) {
      cc_log("Detected use of precompiled header: %s", gchpath);
      pch_file = x_strdup(gchpath);
    } else {
      char* pchpath = format("%s.pch", arg);
      if (Stat::stat(pchpath)) {
        cc_log("Detected use of precompiled header: %s", pchpath);
        pch_file = x_strdup(pchpath);
      } else {
        // clang may use pretokenized headers.
        char* pthpath = format("%s.pth", arg);
        if (Stat::stat(pthpath)) {
          cc_log("Detected use of pretokenized header: %s", pthpath);
          pch_file = x_strdup(pthpath);
        }
        free(pthpath);
      }
      free(pchpath);
    }
    free(gchpath);
  }

  if (pch_file) {
    if (included_pch_file) {
      cc_log("Multiple precompiled headers used: %s and %s\n",
             included_pch_file,
             pch_file);
      stats_update(ctx, STATS_ARGS);
      return false;
    }
    included_pch_file = pch_file;
    *found_pch = true;
  }
  return true;
}

// Process the compiler options into options suitable for passing to the
// preprocessor and the real compiler. preprocessor_args doesn't include -E;
// this is added later. extra_args_to_hash are the arguments that are not
// included in preprocessor_args but that should be included in the hash.
//
// Returns true on success, otherwise false.
bool
cc_process_args(Context& ctx,
                struct args* args,
                struct args** preprocessor_args,
                struct args** extra_args_to_hash,
                struct args** compiler_args)
{
  bool found_c_opt = false;
  bool found_dc_opt = false;
  bool found_S_opt = false;
  bool found_pch = false;
  bool found_fpch_preprocess = false;
  const char* explicit_language = nullptr; // As specified with -x.
  const char* file_language = nullptr;     // As deduced from file extension.
  const char* input_charset = nullptr;

  // Is the dependency makefile name overridden with -MF?
  bool dependency_filename_specified = false;

  // Is the dependency makefile target name specified with -MT or -MQ?
  bool dependency_target_specified = false;

  // Is the dependency target name implicitly specified using
  // DEPENDENCIES_OUTPUT or SUNPRO_DEPENDENCIES?
  bool dependency_implicit_target_specified = false;

  // Is the compiler being asked to output debug info on level 3?
  bool generating_debuginfo_level_3 = false;

  // expanded_args is a copy of the original arguments given to the compiler
  // but with arguments from @file and similar constructs expanded. It's only
  // used as a temporary data structure to loop over.
  struct args* expanded_args = args_copy(args);
  ArgsScopeGuard expanded_args_guard(expanded_args);

  // common_args contains all original arguments except:
  // * those that never should be passed to the preprocessor,
  // * those that only should be passed to the preprocessor (if run_second_cpp
  //   is false), and
  // * dependency options (like -MD and friends).
  struct args* common_args = args_init(0, NULL);
  ArgsScopeGuard common_args_guard(common_args);

  // cpp_args contains arguments that were not added to common_args, i.e. those
  // that should only be passed to the preprocessor if run_second_cpp is false.
  // If run_second_cpp is true, they will be passed to the compiler as well.
  struct args* cpp_args = args_init(0, NULL);
  ArgsScopeGuard cpp_args_guard(cpp_args);

  // dep_args contains dependency options like -MD. They are only passed to the
  // preprocessor, never to the compiler.
  struct args* dep_args = args_init(0, NULL);
  ArgsScopeGuard dep_args_guard(dep_args);

  // compiler_only_args contains arguments that should only be passed to the
  // compiler, not the preprocessor.
  struct args* compiler_only_args = args_init(0, NULL); // will leak on failure

  bool found_color_diagnostics = false;
  bool found_directives_only = false;
  bool found_rewrite_includes = false;

  // Compiler in depend mode is invoked with the original arguments.
  // Collect extra arguments that should be added.
  auto add_extra_arg = [&ctx](const char* arg) {
    if (ctx.config.depend_mode()) {
      if (ctx.args_info.depend_extra_args == nullptr) {
        ctx.args_info.depend_extra_args = args_init(0, nullptr);
      }
      args_add(ctx.args_info.depend_extra_args, arg);
    }
  };

  int argc = expanded_args->argc;
  char** argv = expanded_args->argv;
  args_add(common_args, argv[0]);

  for (int i = 1; i < argc; i++) {
    // The user knows best: just swallow the next arg.
    if (str_eq(argv[i], "--ccache-skip")) {
      i++;
      if (i == argc) {
        cc_log("--ccache-skip lacks an argument");
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      args_add(common_args, argv[i]);
      continue;
    }

    // Special case for -E.
    if (str_eq(argv[i], "-E")) {
      stats_update(ctx, STATS_PREPROCESSING);
      return false;
    }

    // Handle "@file" argument.
    if (str_startswith(argv[i], "@") || str_startswith(argv[i], "-@")) {
      char* argpath = argv[i] + 1;

      if (argpath[-1] == '-') {
        ++argpath;
      }
      struct args* file_args = args_init_from_gcc_atfile(argpath);
      if (!file_args) {
        cc_log("Couldn't read arg file %s", argpath);
        stats_update(ctx, STATS_ARGS);
        return false;
      }

      args_insert(expanded_args, i, file_args, true);
      argc = expanded_args->argc;
      argv = expanded_args->argv;
      i--;
      continue;
    }

    // Handle cuda "-optf" and "--options-file" argument.
    if (guessed_compiler == GUESSED_NVCC
        && (str_eq(argv[i], "-optf") || str_eq(argv[i], "--options-file"))) {
      if (i == argc - 1) {
        cc_log("Expected argument after %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      ++i;

      // Argument is a comma-separated list of files.
      char* str_start = argv[i];
      char* str_end = strchr(str_start, ',');
      int index = i + 1;

      if (!str_end) {
        str_end = str_start + strlen(str_start);
      }

      while (str_end) {
        *str_end = '\0';
        struct args* file_args = args_init_from_gcc_atfile(str_start);
        if (!file_args) {
          cc_log("Couldn't read cuda options file %s", str_start);
          stats_update(ctx, STATS_ARGS);
          return false;
        }

        int new_index = file_args->argc + index;
        args_insert(expanded_args, index, file_args, false);
        index = new_index;
        str_start = str_end;
        str_end = strchr(str_start, ',');
      }

      argc = expanded_args->argc;
      argv = expanded_args->argv;
      continue;
    }

    // These are always too hard.
    if (compopt_too_hard(argv[i]) || str_startswith(argv[i], "-fdump-")
        || str_startswith(argv[i], "-MJ")) {
      cc_log("Compiler option %s is unsupported", argv[i]);
      stats_update(ctx, STATS_UNSUPPORTED_OPTION);
      return false;
    }

    // These are too hard in direct mode.
    if (ctx.config.direct_mode() && compopt_too_hard_for_direct_mode(argv[i])) {
      cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
      ctx.config.set_direct_mode(false);
    }

    // -Xarch_* options are too hard.
    if (str_startswith(argv[i], "-Xarch_")) {
      cc_log("Unsupported compiler option :%s", argv[i]);
      stats_update(ctx, STATS_UNSUPPORTED_OPTION);
      return false;
    }

    // Handle -arch options.
    if (str_eq(argv[i], "-arch")) {
      if (ctx.args_info.arch_args_size == ArgsInfo::max_arch_args - 1) {
        cc_log("Too many -arch compiler options; ccache supports at most %d",
               ArgsInfo::max_arch_args);
        stats_update(ctx, STATS_UNSUPPORTED_OPTION);
        return false;
      }

      ++i;
      ctx.args_info.arch_args[ctx.args_info.arch_args_size] =
        x_strdup(argv[i]); // It will leak.
      ++ctx.args_info.arch_args_size;
      if (ctx.args_info.arch_args_size == 2) {
        ctx.config.set_run_second_cpp(true);
      }
      continue;
    }

    // Handle options that should not be passed to the preprocessor.
    if (compopt_affects_comp(argv[i])) {
      args_add(compiler_only_args, argv[i]);
      if (compopt_takes_arg(argv[i])) {
        if (i == argc - 1) {
          cc_log("Missing argument to %s", argv[i]);
          stats_update(ctx, STATS_ARGS);
          return false;
        }
        args_add(compiler_only_args, argv[i + 1]);
        ++i;
      }
      continue;
    }
    if (compopt_prefix_affects_comp(argv[i])) {
      args_add(compiler_only_args, argv[i]);
      continue;
    }

    if (str_eq(argv[i], "-fpch-preprocess") || str_eq(argv[i], "-emit-pch")
        || str_eq(argv[i], "-emit-pth")) {
      found_fpch_preprocess = true;
    }

    // Modules are handled on demand as necessary in the background,
    // so there is no need to cache them, they can be in practice ignored.
    // All that is needed is to correctly depend also on module.modulemap files,
    // and those are included only in depend mode (preprocessed output does not
    // list them). Still, not including the modules themselves in the hash
    // could possibly result in an object file that would be different
    // from the actual compilation (even though it should be compatible),
    // so require a sloppiness flag.
    if (str_eq(argv[i], "-fmodules")) {
      if (!ctx.config.depend_mode() || !ctx.config.direct_mode()) {
        cc_log("Compiler option %s is unsupported without direct depend mode",
               argv[i]);
        stats_update(ctx, STATS_CANTUSEMODULES);
        return false;
      } else if (!(ctx.config.sloppiness() & SLOPPY_MODULES)) {
        cc_log(
          "You have to specify \"modules\" sloppiness when using"
          " -fmodules to get hits");
        stats_update(ctx, STATS_CANTUSEMODULES);
        return false;
      }
    }

    // We must have -c.
    if (str_eq(argv[i], "-c")) {
      found_c_opt = true;
      continue;
    }

    // when using nvcc with separable compilation, -dc implies -c
    if ((str_eq(argv[i], "-dc") || str_eq(argv[i], "--device-c"))
        && guessed_compiler == GUESSED_NVCC) {
      found_dc_opt = true;
      continue;
    }

    // -S changes the default extension.
    if (str_eq(argv[i], "-S")) {
      args_add(common_args, argv[i]);
      found_S_opt = true;
      continue;
    }

    // Special handling for -x: remember the last specified language before the
    // input file and strip all -x options from the arguments.
    if (str_eq(argv[i], "-x")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      if (ctx.args_info.input_file.empty()) {
        explicit_language = argv[i + 1];
      }
      i++;
      continue;
    }
    if (str_startswith(argv[i], "-x")) {
      if (ctx.args_info.input_file.empty()) {
        explicit_language = &argv[i][2];
      }
      continue;
    }

    // We need to work out where the output was meant to go.
    if (str_eq(argv[i], "-o")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      ctx.args_info.output_obj = make_relative_path(ctx, argv[i + 1]);
      i++;
      continue;
    }

    // Alternate form of -o with no space. Nvcc does not support this.
    if (str_startswith(argv[i], "-o") && guessed_compiler != GUESSED_NVCC) {
      ctx.args_info.output_obj = make_relative_path(ctx, &argv[i][2]);
      continue;
    }

    if (str_startswith(argv[i], "-fdebug-prefix-map=")
        || str_startswith(argv[i], "-ffile-prefix-map=")) {
      ctx.args_info.debug_prefix_maps = static_cast<char**>(
        x_realloc(ctx.args_info.debug_prefix_maps,
                  (ctx.args_info.debug_prefix_maps_len + 1) * sizeof(char*)));
      ctx.args_info.debug_prefix_maps[ctx.args_info.debug_prefix_maps_len++] =
        x_strdup(&argv[i][argv[i][2] == 'f' ? 18 : 19]);
      args_add(common_args, argv[i]);
      continue;
    }

    // Debugging is handled specially, so that we know if we can strip line
    // number info.
    if (str_startswith(argv[i], "-g")) {
      args_add(common_args, argv[i]);

      if (str_startswith(argv[i], "-gdwarf")) {
        // Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
        // debug info on level 2.
        ctx.args_info.generating_debuginfo = true;
        continue;
      }

      if (str_startswith(argv[i], "-gz")) {
        // -gz[=type] neither disables nor enables debug info.
        continue;
      }

      char last_char = argv[i][strlen(argv[i]) - 1];
      if (last_char == '0') {
        // "-g0", "-ggdb0" or similar: All debug information disabled.
        ctx.args_info.generating_debuginfo = false;
        generating_debuginfo_level_3 = false;
      } else {
        ctx.args_info.generating_debuginfo = true;
        if (last_char == '3') {
          generating_debuginfo_level_3 = true;
        }
        if (str_eq(argv[i], "-gsplit-dwarf")) {
          ctx.args_info.seen_split_dwarf = true;
        }
      }
      continue;
    }

    // These options require special handling, because they behave differently
    // with gcc -E, when the output file is not specified.
    if (str_eq(argv[i], "-MD") || str_eq(argv[i], "-MMD")) {
      ctx.args_info.generating_dependencies = true;
      args_add(dep_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-MF")) {
      dependency_filename_specified = true;

      char* arg;
      bool separate_argument = (strlen(argv[i]) == 3);
      if (separate_argument) {
        // -MF arg
        if (i == argc - 1) {
          cc_log("Missing argument to %s", argv[i]);
          stats_update(ctx, STATS_ARGS);
          return false;
        }
        arg = argv[i + 1];
        i++;
      } else {
        // -MFarg or -MF=arg (EDG-based compilers)
        arg = &argv[i][3];
        if (arg[0] == '=') {
          ++arg;
        }
      }
      ctx.args_info.output_dep = make_relative_path(ctx, arg);
      // Keep the format of the args the same.
      if (separate_argument) {
        args_add(dep_args, "-MF");
        args_add(dep_args, ctx.args_info.output_dep.c_str());
      } else {
        char* option = format("-MF%s", ctx.args_info.output_dep.c_str());
        args_add(dep_args, option);
        free(option);
      }
      continue;
    }
    if (str_startswith(argv[i], "-MQ") || str_startswith(argv[i], "-MT")) {
      dependency_target_specified = true;

      if (strlen(argv[i]) == 3) {
        // -MQ arg or -MT arg
        if (i == argc - 1) {
          cc_log("Missing argument to %s", argv[i]);
          stats_update(ctx, STATS_ARGS);
          return false;
        }
        args_add(dep_args, argv[i]);
        std::string relpath = make_relative_path(ctx, argv[i + 1]);
        args_add(dep_args, relpath.c_str());
        i++;
      } else {
        char* arg_opt = x_strndup(argv[i], 3);
        std::string relpath = make_relative_path(ctx, argv[i] + 3);
        char* option = format("%s%s", arg_opt, relpath.c_str());
        args_add(dep_args, option);
        free(arg_opt);
        free(option);
      }
      continue;
    }
    if (str_eq(argv[i], "-fprofile-arcs")) {
      ctx.args_info.profile_arcs = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "-ftest-coverage")) {
      ctx.args_info.generating_coverage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "-fstack-usage")) {
      ctx.args_info.generating_stackusage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "--coverage")      // = -fprofile-arcs -ftest-coverage
        || str_eq(argv[i], "-coverage")) { // Undocumented but still works.
      ctx.args_info.profile_arcs = true;
      ctx.args_info.generating_coverage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-fprofile-dir=")) {
      ctx.args_info.profile_dir.assign(argv[i] + 14);
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-fsanitize-blacklist=")) {
      ctx.args_info.sanitize_blacklists = static_cast<char**>(
        x_realloc(ctx.args_info.sanitize_blacklists,
                  (ctx.args_info.sanitize_blacklists_len + 1) * sizeof(char*)));
      ctx.args_info
        .sanitize_blacklists[ctx.args_info.sanitize_blacklists_len++] =
        x_strdup(argv[i] + 21);
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "--sysroot=")) {
      std::string relpath = make_relative_path(ctx, argv[i] + 10);
      std::string option = fmt::format("--sysroot={}", relpath);
      args_add(common_args, option.c_str());
      continue;
    }
    // Alternate form of specifying sysroot without =
    if (str_eq(argv[i], "--sysroot")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      args_add(common_args, argv[i]);
      std::string relpath = make_relative_path(ctx, argv[i + 1]);
      args_add(common_args, relpath.c_str());
      i++;
      continue;
    }
    // Alternate form of specifying target without =
    if (str_eq(argv[i], "-target")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      args_add(common_args, argv[i]);
      args_add(common_args, argv[i + 1]);
      i++;
      continue;
    }
    if (str_startswith(argv[i], "-Wp,")) {
      if (str_eq(argv[i], "-Wp,-P") || strstr(argv[i], ",-P,")
          || str_endswith(argv[i], ",-P")) {
        // -P removes preprocessor information in such a way that the object
        // file from compiling the preprocessed file will not be equal to the
        // object file produced when compiling without ccache.
        cc_log("Too hard option -Wp,-P detected");
        stats_update(ctx, STATS_UNSUPPORTED_OPTION);
        failed(ctx);
      } else if (str_startswith(argv[i], "-Wp,-MD,")
                 && !strchr(argv[i] + 8, ',')) {
        ctx.args_info.generating_dependencies = true;
        dependency_filename_specified = true;
        ctx.args_info.output_dep = make_relative_path(ctx, argv[i] + 8);
        args_add(dep_args, argv[i]);
        continue;
      } else if (str_startswith(argv[i], "-Wp,-MMD,")
                 && !strchr(argv[i] + 9, ',')) {
        ctx.args_info.generating_dependencies = true;
        dependency_filename_specified = true;
        ctx.args_info.output_dep = make_relative_path(ctx, argv[i] + 9);
        args_add(dep_args, argv[i]);
        continue;
      } else if (str_startswith(argv[i], "-Wp,-D")
                 && !strchr(argv[i] + 6, ',')) {
        // Treat it like -D.
        args_add(cpp_args, argv[i] + 4);
        continue;
      } else if (str_eq(argv[i], "-Wp,-MP")
                 || (strlen(argv[i]) > 8 && str_startswith(argv[i], "-Wp,-M")
                     && argv[i][7] == ','
                     && (argv[i][6] == 'F' || argv[i][6] == 'Q'
                         || argv[i][6] == 'T')
                     && !strchr(argv[i] + 8, ','))) {
        // TODO: Make argument to MF/MQ/MT relative.
        args_add(dep_args, argv[i]);
        continue;
      } else if (ctx.config.direct_mode()) {
        // -Wp, can be used to pass too hard options to the preprocessor.
        // Hence, disable direct mode.
        cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
        ctx.config.set_direct_mode(false);
      }

      // Any other -Wp,* arguments are only relevant for the preprocessor.
      args_add(cpp_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "-MP")) {
      args_add(dep_args, argv[i]);
      continue;
    }

    // Input charset needs to be handled specially.
    if (str_startswith(argv[i], "-finput-charset=")) {
      input_charset = argv[i];
      continue;
    }

    if (str_eq(argv[i], "--serialize-diagnostics")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      ctx.args_info.generating_diagnostics = true;
      ctx.args_info.output_dia = make_relative_path(ctx, argv[i + 1]);
      i++;
      continue;
    }

    if (str_startswith(argv[i], "-fprofile-")) {
      char* arg = x_strdup(argv[i]);
      const char* arg_profile_dir = strchr(argv[i], '=');
      if (arg_profile_dir) {
        // Convert to absolute path.
        char* dir = x_realpath(arg_profile_dir + 1);
        if (!dir) {
          // Directory doesn't exist.
          dir = x_strdup(arg_profile_dir + 1);
        }

        // We can get a better hit rate by using the real path here.
        free(arg);
        char* option = x_strndup(argv[i], arg_profile_dir - argv[i]);
        arg = format("%s=%s", option, dir);
        cc_log("Rewriting %s to %s", argv[i], arg);
        free(option);
        free(dir);
      }

      bool supported_profile_option = false;
      if (str_startswith(argv[i], "-fprofile-generate")
          || str_eq(argv[i], "-fprofile-arcs")) {
        ctx.args_info.profile_generate = true;
        supported_profile_option = true;
      } else if (str_startswith(argv[i], "-fprofile-use")
                 || str_eq(argv[i], "-fbranch-probabilities")) {
        ctx.args_info.profile_use = true;
        supported_profile_option = true;
      } else if (str_eq(argv[i], "-fprofile-dir")) {
        supported_profile_option = true;
      }

      if (supported_profile_option) {
        args_add(common_args, arg);
        free(arg);

        // If the profile directory has already been set, give up... Hard to
        // know what the user means, and what the compiler will do.
        if (arg_profile_dir && !ctx.args_info.profile_dir.empty()) {
          cc_log("Profile directory already set; giving up");
          stats_update(ctx, STATS_UNSUPPORTED_OPTION);
          return false;
        } else if (arg_profile_dir) {
          cc_log("Setting profile directory to %s", arg_profile_dir);
          ctx.args_info.profile_dir = from_cstr(arg_profile_dir);
        }
        continue;
      }
      cc_log("Unknown profile option: %s", argv[i]);
      free(arg);
    }

    if (str_eq(argv[i], "-fcolor-diagnostics")
        || str_eq(argv[i], "-fno-color-diagnostics")
        || str_eq(argv[i], "-fdiagnostics-color")
        || str_eq(argv[i], "-fdiagnostics-color=always")
        || str_eq(argv[i], "-fno-diagnostics-color")
        || str_eq(argv[i], "-fdiagnostics-color=never")) {
      args_add(common_args, argv[i]);
      found_color_diagnostics = true;
      continue;
    }
    if (str_eq(argv[i], "-fdiagnostics-color=auto")) {
      if (color_output_possible()) {
        // Output is redirected, so color output must be forced.
        args_add(common_args, "-fdiagnostics-color=always");
        add_extra_arg("-fdiagnostics-color=always");
        cc_log("Automatically forcing colors");
      } else {
        args_add(common_args, argv[i]);
      }
      found_color_diagnostics = true;
      continue;
    }

    // GCC
    if (str_eq(argv[i], "-fdirectives-only")) {
      found_directives_only = true;
      continue;
    }
    // Clang
    if (str_eq(argv[i], "-frewrite-includes")) {
      found_rewrite_includes = true;
      continue;
    }

    if (ctx.config.sloppiness() & SLOPPY_CLANG_INDEX_STORE
        && str_eq(argv[i], "-index-store-path")) {
      // Xcode 9 or later calls Clang with this option. The given path includes
      // a UUID that might lead to cache misses, especially when cache is
      // shared among multiple users.
      i++;
      if (i <= argc - 1) {
        cc_log("Skipping argument -index-store-path %s", argv[i]);
      }
      continue;
    }

    // Options taking an argument that we may want to rewrite to relative paths
    // to get better hit rate. A secondary effect is that paths in the standard
    // error output produced by the compiler will be normalized.
    if (compopt_takes_path(argv[i])) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }
      if (!detect_pch(ctx, argv[i], argv[i + 1], &found_pch)) {
        return false;
      }

      std::string relpath = make_relative_path(ctx, argv[i + 1]);
      if (compopt_affects_cpp(argv[i])) {
        args_add(cpp_args, argv[i]);
        args_add(cpp_args, relpath.c_str());
      } else {
        args_add(common_args, argv[i]);
        args_add(common_args, relpath.c_str());
      }

      i++;
      continue;
    }

    // Same as above but options with concatenated argument beginning with a
    // slash.
    if (argv[i][0] == '-') {
      char* slash_pos = strchr(argv[i], '/');
      if (slash_pos) {
        char* option = x_strndup(argv[i], slash_pos - argv[i]);
        if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
          std::string relpath = make_relative_path(ctx, slash_pos);
          char* new_option = format("%s%s", option, relpath.c_str());
          if (compopt_affects_cpp(option)) {
            args_add(cpp_args, new_option);
          } else {
            args_add(common_args, new_option);
          }
          free(new_option);
          free(option);
          continue;
        } else {
          free(option);
        }
      }
    }

    // Options that take an argument.
    if (compopt_takes_arg(argv[i])) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        stats_update(ctx, STATS_ARGS);
        return false;
      }

      if (compopt_affects_cpp(argv[i])) {
        args_add(cpp_args, argv[i]);
        args_add(cpp_args, argv[i + 1]);
      } else {
        args_add(common_args, argv[i]);
        args_add(common_args, argv[i + 1]);
      }

      i++;
      continue;
    }

    // Other options.
    if (argv[i][0] == '-') {
      if (compopt_affects_cpp(argv[i]) || compopt_prefix_affects_cpp(argv[i])) {
        args_add(cpp_args, argv[i]);
      } else {
        args_add(common_args, argv[i]);
      }
      continue;
    }

    // If an argument isn't a plain file then assume its an option, not an
    // input file. This allows us to cope better with unusual compiler options.
    //
    // Note that "/dev/null" is an exception that is sometimes used as an input
    // file when code is testing compiler flags.
    if (!str_eq(argv[i], "/dev/null")) {
      auto st = Stat::stat(argv[i]);
      if (!st || !st.is_regular()) {
        cc_log("%s is not a regular file, not considering as input file",
               argv[i]);
        args_add(common_args, argv[i]);
        continue;
      }
    }

    if (!ctx.args_info.input_file.empty()) {
      if (language_for_file(argv[i])) {
        cc_log("Multiple input files: %s and %s",
               ctx.args_info.input_file.c_str(),
               argv[i]);
        stats_update(ctx, STATS_MULTIPLE);
      } else if (!found_c_opt && !found_dc_opt) {
        cc_log("Called for link with %s", argv[i]);
        if (strstr(argv[i], "conftest.")) {
          stats_update(ctx, STATS_CONFTEST);
        } else {
          stats_update(ctx, STATS_LINK);
        }
      } else {
        cc_log("Unsupported source extension: %s", argv[i]);
        stats_update(ctx, STATS_SOURCELANG);
      }
      return false;
    }

    // The source code file path gets put into the notes.
    if (ctx.args_info.generating_coverage) {
      ctx.args_info.input_file = from_cstr(argv[i]);
      continue;
    }

    if (Stat::lstat(argv[i], Stat::OnError::log).is_symlink()) {
      // Don't rewrite source file path if it's a symlink since
      // make_relative_path resolves symlinks using realpath(3) and this leads
      // to potentially choosing incorrect relative header files. See the
      // "symlink to source file" test.
      ctx.args_info.input_file = from_cstr(argv[i]);
    } else {
      // Rewrite to relative to increase hit rate.
      ctx.args_info.input_file = make_relative_path(ctx, argv[i]);
    }
  } // for

  if (generating_debuginfo_level_3 && !ctx.config.run_second_cpp()) {
    cc_log("Generating debug info level 3; not compiling preprocessed code");
    ctx.config.set_run_second_cpp(true);
  }

  // See <http://gcc.gnu.org/onlinedocs/cpp/Environment-Variables.html>.
  // Contrary to what the documentation seems to imply the compiler still
  // creates object files with these defined (confirmed with GCC 8.2.1), i.e.
  // they work as -MMD/-MD, not -MM/-M. These environment variables do nothing
  // on Clang.
  {
    char* dependencies_env = getenv("DEPENDENCIES_OUTPUT");
    bool using_sunpro_dependencies = false;
    if (!dependencies_env) {
      dependencies_env = getenv("SUNPRO_DEPENDENCIES");
      using_sunpro_dependencies = true;
    }
    if (dependencies_env) {
      ctx.args_info.generating_dependencies = true;
      dependency_filename_specified = true;
      char* saveptr = nullptr;
      char* abspath_file = strtok_r(dependencies_env, " ", &saveptr);

      ctx.args_info.output_dep = make_relative_path(ctx, abspath_file);

      // specifying target object is optional.
      char* abspath_obj = strtok_r(nullptr, " ", &saveptr);
      if (abspath_obj) {
        // it's the "file target" form.

        dependency_target_specified = true;
        std::string relpath_obj = make_relative_path(ctx, abspath_obj);
        // ensure compiler gets relative path.
        std::string relpath_both =
          fmt::format("{} {}", ctx.args_info.output_dep, relpath_obj);
        if (using_sunpro_dependencies) {
          x_setenv("SUNPRO_DEPENDENCIES", relpath_both.c_str());
        } else {
          x_setenv("DEPENDENCIES_OUTPUT", relpath_both.c_str());
        }
      } else {
        // it's the "file" form.

        dependency_implicit_target_specified = true;
        // ensure compiler gets relative path.
        if (using_sunpro_dependencies) {
          x_setenv("SUNPRO_DEPENDENCIES", ctx.args_info.output_dep.c_str());
        } else {
          x_setenv("DEPENDENCIES_OUTPUT", ctx.args_info.output_dep.c_str());
        }
      }
    }
  }

  if (ctx.args_info.input_file.empty()) {
    cc_log("No input file found");
    stats_update(ctx, STATS_NOINPUT);
    return false;
  }

  if (found_pch || found_fpch_preprocess) {
    ctx.args_info.using_precompiled_header = true;
    if (!(ctx.config.sloppiness() & SLOPPY_TIME_MACROS)) {
      cc_log(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      cc_log("Disabling direct mode");
      stats_update(ctx, STATS_CANTUSEPCH);
      return false;
    }
  }

  if (explicit_language && str_eq(explicit_language, "none")) {
    explicit_language = nullptr;
  }
  file_language = language_for_file(ctx.args_info.input_file.c_str());
  if (explicit_language) {
    if (!language_is_supported(explicit_language)) {
      cc_log("Unsupported language: %s", explicit_language);
      stats_update(ctx, STATS_SOURCELANG);
      return false;
    }
    ctx.args_info.actual_language = from_cstr(explicit_language);
  } else {
    ctx.args_info.actual_language = from_cstr(file_language);
  }

  ctx.args_info.output_is_precompiled_header =
    ctx.args_info.actual_language.find("-header") != std::string::npos;

  if (ctx.args_info.output_is_precompiled_header
      && !(ctx.config.sloppiness() & SLOPPY_PCH_DEFINES)) {
    cc_log(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    stats_update(ctx, STATS_CANTUSEPCH);
    return false;
  }

  if (!found_c_opt && !found_dc_opt && !found_S_opt) {
    if (ctx.args_info.output_is_precompiled_header) {
      args_add(common_args, "-c");
    } else {
      cc_log("No -c option found");
      // I find that having a separate statistic for autoconf tests is useful,
      // as they are the dominant form of "called for link" in many cases.
      if (ctx.args_info.input_file.find("conftest.") != std::string::npos) {
        stats_update(ctx, STATS_CONFTEST);
      } else {
        stats_update(ctx, STATS_LINK);
      }
      return false;
    }
  }

  if (ctx.args_info.actual_language.empty()) {
    cc_log("Unsupported source extension: %s",
           ctx.args_info.input_file.c_str());
    stats_update(ctx, STATS_SOURCELANG);
    return false;
  }

  if (!ctx.config.run_second_cpp() && ctx.args_info.actual_language == "cu") {
    cc_log("Using CUDA compiler; not compiling preprocessed code");
    ctx.config.set_run_second_cpp(true);
  }

  ctx.args_info.direct_i_file =
    language_is_preprocessed(ctx.args_info.actual_language.c_str());

  if (ctx.args_info.output_is_precompiled_header
      && !ctx.config.run_second_cpp()) {
    // It doesn't work to create the .gch from preprocessed source.
    cc_log("Creating precompiled header; not compiling preprocessed code");
    ctx.config.set_run_second_cpp(true);
  }

  if (ctx.config.cpp_extension().empty()) {
    const char* p_language =
      p_language_for_language(ctx.args_info.actual_language.c_str());
    ctx.config.set_cpp_extension(extension_for_language(p_language) + 1);
  }

  // Don't try to second guess the compilers heuristics for stdout handling.
  if (ctx.args_info.output_obj == "-") {
    stats_update(ctx, STATS_OUTSTDOUT);
    cc_log("Output file is -");
    return false;
  }

  if (ctx.args_info.output_obj.empty()) {
    if (ctx.args_info.output_is_precompiled_header) {
      ctx.args_info.output_obj = ctx.args_info.input_file + ".gch";
    } else {
      string_view extension = found_S_opt ? ".s" : ".o";
      ctx.args_info.output_obj = Util::change_extension(
        Util::base_name(ctx.args_info.input_file), extension);
    }
  }

  if (ctx.args_info.seen_split_dwarf) {
    size_t pos = ctx.args_info.output_obj.rfind('.');
    if (pos == std::string::npos
        || pos == ctx.args_info.output_obj.size() - 1) {
      cc_log("Badly formed object filename");
      stats_update(ctx, STATS_ARGS);
      return false;
    }

    ctx.args_info.output_dwo =
      Util::change_extension(ctx.args_info.output_obj, ".dwo");
  }

  // Cope with -o /dev/null.
  if (ctx.args_info.output_obj != "/dev/null") {
    auto st = Stat::stat(ctx.args_info.output_obj);
    if (st && !st.is_regular()) {
      cc_log("Not a regular file: %s", ctx.args_info.output_obj.c_str());
      stats_update(ctx, STATS_BADOUTPUTFILE);
      return false;
    }
  }

  {
    char* output_dir = x_dirname(ctx.args_info.output_obj.c_str());
    auto st = Stat::stat(output_dir);
    if (!st || !st.is_directory()) {
      cc_log("Directory does not exist: %s", output_dir);
      stats_update(ctx, STATS_BADOUTPUTFILE);
      free(output_dir);
      return false;
    }
    free(output_dir);
  }

  // Some options shouldn't be passed to the real compiler when it compiles
  // preprocessed code:
  //
  // -finput-charset=XXX (otherwise conversion happens twice)
  // -x XXX (otherwise the wrong language is selected)
  if (input_charset) {
    args_add(cpp_args, input_charset);
  }
  if (found_pch) {
    args_add(cpp_args, "-fpch-preprocess");
  }
  if (explicit_language) {
    args_add(cpp_args, "-x");
    args_add(cpp_args, explicit_language);
  }

  // Since output is redirected, compilers will not color their output by
  // default, so force it explicitly if it would be otherwise done.
  if (!found_color_diagnostics && color_output_possible()) {
    if (guessed_compiler == GUESSED_CLANG) {
      if (ctx.args_info.actual_language != "assembler") {
        args_add(common_args, "-fcolor-diagnostics");
        add_extra_arg("-fcolor-diagnostics");
        cc_log("Automatically enabling colors");
      }
    } else if (guessed_compiler == GUESSED_GCC) {
      // GCC has it since 4.9, but that'd require detecting what GCC version is
      // used for the actual compile. However it requires also GCC_COLORS to be
      // set (and not empty), so use that for detecting if GCC would use
      // colors.
      if (getenv("GCC_COLORS") && getenv("GCC_COLORS")[0] != '\0') {
        args_add(common_args, "-fdiagnostics-color");
        add_extra_arg("-fdiagnostics-color");
        cc_log("Automatically enabling colors");
      }
    }
  }

  if (ctx.args_info.generating_dependencies) {
    if (!dependency_filename_specified) {
      std::string default_depfile_name =
        Util::change_extension(ctx.args_info.output_obj, ".d");
      ctx.args_info.output_dep =
        make_relative_path(ctx, default_depfile_name.c_str());
      if (!g_config.run_second_cpp()) {
        // If we're compiling preprocessed code we're sending dep_args to the
        // preprocessor so we need to use -MF to write to the correct .d file
        // location since the preprocessor doesn't know the final object path.
        args_add(dep_args, "-MF");
        args_add(dep_args, default_depfile_name.c_str());
      }
    }

    if (!dependency_target_specified && !dependency_implicit_target_specified
        && !g_config.run_second_cpp()) {
      // If we're compiling preprocessed code we're sending dep_args to the
      // preprocessor so we need to use -MQ to get the correct target object
      // file in the .d file.
      args_add(dep_args, "-MQ");
      args_add(dep_args, ctx.args_info.output_obj.c_str());
    }
  }
  if (ctx.args_info.generating_coverage) {
    std::string gcda_path =
      Util::change_extension(ctx.args_info.output_obj, ".gcno");
    ctx.args_info.output_cov = make_relative_path(ctx, gcda_path.c_str());
  }
  if (ctx.args_info.generating_stackusage) {
    std::string default_sufile_name =
      Util::change_extension(ctx.args_info.output_obj, ".su");
    ctx.args_info.output_su =
      make_relative_path(ctx, default_sufile_name.c_str());
  }

  *compiler_args = args_copy(common_args);
  args_extend(*compiler_args, compiler_only_args);

  if (ctx.config.run_second_cpp()) {
    args_extend(*compiler_args, cpp_args);
  } else if (found_directives_only || found_rewrite_includes) {
    // Need to pass the macros and any other preprocessor directives again.
    args_extend(*compiler_args, cpp_args);
    if (found_directives_only) {
      args_add(cpp_args, "-fdirectives-only");
      // The preprocessed source code still needs some more preprocessing.
      args_add(*compiler_args, "-fpreprocessed");
      args_add(*compiler_args, "-fdirectives-only");
    }
    if (found_rewrite_includes) {
      args_add(cpp_args, "-frewrite-includes");
      // The preprocessed source code still needs some more preprocessing.
      args_add(*compiler_args, "-x");
      args_add(*compiler_args, ctx.args_info.actual_language.c_str());
    }
  } else if (explicit_language) {
    // Workaround for a bug in Apple's patched distcc -- it doesn't properly
    // reset the language specified with -x, so if -x is given, we have to
    // specify the preprocessed language explicitly.
    args_add(*compiler_args, "-x");
    args_add(*compiler_args, p_language_for_language(explicit_language));
  }

  if (found_c_opt) {
    args_add(*compiler_args, "-c");
  }

  if (found_dc_opt) {
    args_add(*compiler_args, "-dc");
  }

  for (size_t i = 0; i < ctx.args_info.arch_args_size; ++i) {
    args_add(*compiler_args, "-arch");
    args_add(*compiler_args, ctx.args_info.arch_args[i]);
  }

  *preprocessor_args = args_copy(common_args);
  args_extend(*preprocessor_args, cpp_args);

  if (g_config.run_second_cpp()) {
    // When not compiling the preprocessed source code, only pass dependency
    // arguments to the compiler to avoid having to add -MQ, supporting e.g.
    // EDG-based compilers which don't support -MQ.
    args_extend(*compiler_args, dep_args);
  } else {
    // When compiling the preprocessed source code, pass dependency arguments to
    // the preprocessor since the compiler doesn't produce a .d file when
    // compiling preprocessed source code.
    args_extend(*preprocessor_args, dep_args);
  }

  *extra_args_to_hash = compiler_only_args;
  if (g_config.run_second_cpp()) {
    args_extend(*extra_args_to_hash, dep_args);
  }

  return true;
}

static void
create_initial_config_file(Config& cfg,
                           const std::string& path) // TODO, remove 2nd arg
{
  if (!Util::create_dir(Util::dir_name(path))) {
    return;
  }

  unsigned max_files;
  uint64_t max_size;
  char* stats_dir = format("%s/0", g_config.cache_dir().c_str());
  if (Stat::stat(stats_dir)) {
    stats_get_obsolete_limits(stats_dir, &max_files, &max_size);
    // STATS_MAXFILES and STATS_MAXSIZE was stored for each top directory.
    max_files *= 16;
    max_size *= 16;
  } else {
    max_files = 0;
    max_size = g_config.max_size();
  }
  free(stats_dir);

  FILE* f = fopen(path.c_str(), "w");
  if (!f) {
    return;
  }
  if (max_files != 0) {
    fprintf(f, "max_files = %u\n", max_files);
    g_config.set_max_files(max_files);
  }
  if (max_size != 0) {
    char* size = format_parsable_size_with_suffix(max_size);
    fprintf(f, "max_size = %s\n", size);
    free(size);
    g_config.set_max_size(max_size);
  }
  fclose(f);
}

#ifdef MTR_ENABLED
static void* trace_id;
static char* tmp_trace_file;

static void
trace_init(char* path)
{
  tmp_trace_file = path;
  mtr_init(tmp_trace_file);
  char* s = format("%f", time_seconds());
  MTR_INSTANT_C("", "", "time", s);
}

static void
trace_start(void)
{
  MTR_META_PROCESS_NAME(MYNAME);
  trace_id = (void*)((long)getpid());
  MTR_START("program", "ccache", trace_id);
}

static void
trace_stop(void)
{
  char* trace_file = format("%s.ccache-trace", output_obj);
  MTR_FINISH("program", "ccache", trace_id);
  mtr_flush();
  mtr_shutdown();
  move_file(tmp_trace_file, trace_file);
  free(trace_file);
  free(tmp_trace_file);
}

static const char*
tmpdir()
{
#  ifndef _WIN32
  const char* tmpdir = getenv("TMPDIR");
  if (tmpdir != NULL) {
    return tmpdir;
  }
#  else
  static char dirbuf[PATH_MAX];
  DWORD retval = GetTempPath(PATH_MAX, dirbuf);
  if (retval > 0 && retval < PATH_MAX) {
    return dirbuf;
  }
#  endif
  return "/tmp";
}

#endif // MTR_ENABLED

// Read config file(s), populate variables, create configuration file in cache
// directory if missing, etc.
static void
initialize(Config& cfg)
{
  bool enable_internal_trace = getenv("CCACHE_INTERNAL_TRACE");
  if (enable_internal_trace) {
#ifdef MTR_ENABLED
    // We don't have any conf yet, so we can't use temp_dir() here.
    trace_init(format("%s/tmp.ccache-trace.%d", tmpdir(), (int)getpid()));
#endif
  }

  char* p = getenv("CCACHE_CONFIGPATH");
  if (p) {
    g_config.set_primary_config_path(p);
  } else {
    g_config.set_secondary_config_path(
      fmt::format("{}/ccache.conf", TO_STRING(SYSCONFDIR)));
    MTR_BEGIN("config", "conf_read_secondary");
    // A missing config file in SYSCONFDIR is OK so don't check return value.
    g_config.update_from_file(g_config.secondary_config_path());
    MTR_END("config", "conf_read_secondary");

    if (g_config.cache_dir().empty()) {
      fatal("configuration setting \"cache_dir\" must not be the empty string");
    }
    if ((p = getenv("CCACHE_DIR"))) {
      g_config.set_cache_dir(p);
    }
    if (g_config.cache_dir().empty()) {
      fatal("CCACHE_DIR must not be the empty string");
    }

    g_config.set_primary_config_path(
      fmt::format("{}/ccache.conf", g_config.cache_dir()));
  }

  bool should_create_initial_config = false;
  MTR_BEGIN("config", "conf_read_primary");
  if (!g_config.update_from_file(g_config.primary_config_path())
      && !g_config.disable()) {
    should_create_initial_config = true;
  }
  MTR_END("config", "conf_read_primary");

  MTR_BEGIN("config", "conf_update_from_environment");
  g_config.update_from_environment();
  MTR_END("config", "conf_update_from_environment");

  if (should_create_initial_config) {
    create_initial_config_file(cfg, g_config.primary_config_path());
  }

  exitfn_init();
  // TODO, context
  // exitfn_add_nullary(stats_flush);
  exitfn_add_nullary(clean_up_pending_tmp_files);

  cc_log("=== CCACHE %s STARTED =========================================",
         CCACHE_VERSION);

  if (g_config.umask() != std::numeric_limits<uint32_t>::max()) {
    umask(g_config.umask());
  }

  if (enable_internal_trace) {
#ifdef MTR_ENABLED
    trace_start();
    exitfn_add_nullary(trace_stop);
#else
    cc_log("Error: tracing is not enabled!");
#endif
  }
}

template<class T>
static void
free_and_nullify(T*& ptr)
{
  free(ptr);
  ptr = NULL;
}

// Reset the global state. Used by the test suite.
void
cc_reset(void)
{
  g_config.clear_and_reset();

  free_and_nullify(current_working_dir);
  free_and_nullify(profile_dir);
  free_and_nullify(included_pch_file);
  args_free(orig_args);
  orig_args = NULL;
  free_and_nullify(cached_result_name);
  free_and_nullify(cached_result_path);
  free_and_nullify(manifest_path);
  time_of_compilation = 0;
  for (size_t i = 0; i < ignore_headers_len; i++) {
    free_and_nullify(ignore_headers[i]);
  }
  free_and_nullify(ignore_headers);
  ignore_headers_len = 0;
  g_included_files.clear();
  has_absolute_include_headers = false;
  generating_dependencies = false;
  generating_coverage = false;
  generating_stackusage = false;
  profile_arcs = false;
  i_tmpfile = NULL;
  direct_i_file = false;
  free_and_nullify(cpp_stderr);
  free_and_nullify(stats_file);
  output_is_precompiled_header = false;

  seen_split_dwarf = false;
}

// Make a copy of stderr that will not be cached, so things like distcc can
// send networking errors to it.
static void
set_up_uncached_err(Context& ctx)
{
  int uncached_fd = dup(2); // The file descriptor is intentionally leaked.
  if (uncached_fd == -1) {
    cc_log("dup(2) failed: %s", strerror(errno));
    stats_update(ctx, STATS_ERROR);
    failed(ctx);
  }

  // Leak a pointer to the environment.
  char* buf = format("UNCACHED_ERR_FD=%d", uncached_fd);
  if (putenv(buf) == -1) {
    cc_log("putenv failed: %s", strerror(errno));
    stats_update(ctx, STATS_ERROR);
    failed(ctx);
  }
}

static void
configuration_logger(const std::string& key,
                     const std::string& value,
                     const std::string& origin)
{
  cc_bulklog(
    "Config: (%s) %s = %s", origin.c_str(), key.c_str(), value.c_str());
}

static void
configuration_printer(const std::string& key,
                      const std::string& value,
                      const std::string& origin)
{
  fmt::print("({}) {} = {}\n", origin, key, value);
}

static void ccache(Context& ctx, int argc, char* argv[]) ATTR_NORETURN;

// The main ccache driver function.
static void
ccache(Context& ctx, int argc, char* argv[])
{
#ifndef _WIN32
  set_up_signal_handlers();
#endif

  // Needed for portability when using localtime_r.
  tzset();

  orig_args = args_init(argc, argv);

  initialize(ctx.config);
  MTR_BEGIN("main", "find_compiler");
  find_compiler(ctx, argv);
  MTR_END("main", "find_compiler");

  MTR_BEGIN("main", "clean_up_internal_tempdir");
  if (g_config.temporary_dir().empty()) {
    clean_up_internal_tempdir(ctx.config);
  }
  MTR_END("main", "clean_up_internal_tempdir");

  if (!g_config.log_file().empty() || g_config.debug()) {
    g_config.visit_items(configuration_logger);
  }

  if (g_config.disable()) {
    cc_log("ccache is disabled");
    stats_update(ctx, STATS_CACHEMISS); // Dummy to trigger stats_flush.
    failed(ctx);
  }

  MTR_BEGIN("main", "set_up_uncached_err");
  set_up_uncached_err(ctx);
  MTR_END("main", "set_up_uncached_err");

  cc_log_argv("Command line: ", argv);
  cc_log("Hostname: %s", get_hostname());
  cc_log("Working directory: %s", get_current_working_dir(ctx));

  g_config.set_limit_multiple(
    std::min(std::max(g_config.limit_multiple(), 0.0), 1.0));

  MTR_BEGIN("main", "guess_compiler");
  guessed_compiler = guess_compiler(orig_args->argv[0]);
  MTR_END("main", "guess_compiler");

  // Arguments (except -E) to send to the preprocessor.
  struct args* preprocessor_args;
  // Arguments not sent to the preprocessor but that should be part of the
  // hash.
  struct args* extra_args_to_hash;
  // Arguments to send to the real compiler.
  struct args* compiler_args;
  MTR_BEGIN("main", "process_args");

  if (!cc_process_args(ctx,
                       orig_args,
                       &preprocessor_args,
                       &extra_args_to_hash,
                       &compiler_args)) {
    failed(ctx); // stats_update is called in cc_process_ar
  }

  actual_language = x_strdup(ctx.args_info.actual_language.c_str());

  generating_dependencies = ctx.args_info.generating_dependencies;
  generating_coverage = ctx.args_info.generating_coverage;
  generating_stackusage = ctx.args_info.generating_stackusage;
  generating_diagnostics = ctx.args_info.generating_diagnostics;
  seen_split_dwarf = ctx.args_info.seen_split_dwarf;
  profile_arcs = ctx.args_info.profile_arcs;
  profile_dir = x_strdup(ctx.args_info.profile_dir.c_str());

  direct_i_file = ctx.args_info.direct_i_file;
  output_is_precompiled_header = ctx.args_info.output_is_precompiled_header;
  profile_use = ctx.args_info.profile_use;
  profile_generate = ctx.args_info.profile_generate;
  using_precompiled_header = ctx.args_info.using_precompiled_header;

  arch_args_size = ctx.args_info.arch_args_size;
  for (size_t i = 0; i < ctx.args_info.arch_args_size; ++i) {
    arch_args[i] = x_strdup(ctx.args_info.arch_args[i]);
  }

  MTR_END("main", "process_args");

  if (g_config.depend_mode()
      && (!generating_dependencies || ctx.args_info.output_dep == "/dev/null"
          || !g_config.run_second_cpp())) {
    cc_log("Disabling depend mode");
    g_config.set_depend_mode(false);
  }

  cc_log("Source file: %s", ctx.args_info.input_file.c_str());
  if (generating_dependencies) {
    cc_log("Dependency file: %s", ctx.args_info.output_dep.c_str());
  }
  if (generating_coverage) {
    cc_log("Coverage file: %s", ctx.args_info.output_cov.c_str());
  }
  if (generating_stackusage) {
    cc_log("Stack usage file: %s", ctx.args_info.output_su.c_str());
  }
  if (generating_diagnostics) {
    cc_log("Diagnostics file: %s", ctx.args_info.output_dia.c_str());
  }
  if (!ctx.args_info.output_dwo.empty()) {
    cc_log("Split dwarf file: %s", ctx.args_info.output_dwo.c_str());
  }

  cc_log("Object file: %s", ctx.args_info.output_obj.c_str());
  MTR_META_THREAD_NAME(ctx.args_info.output_obj);

  // Need to dump log buffer as the last exit function to not lose any logs.
  char* non_const_output_obj =
    const_cast<char*>(ctx.args_info.output_obj.c_str());
  exitfn_add_last(dump_debug_log_buffer_exitfn, non_const_output_obj);

  FILE* debug_text_file = NULL;
  if (g_config.debug()) {
    char* path =
      format("%s.ccache-input-text", ctx.args_info.output_obj.c_str());
    debug_text_file = fopen(path, "w");
    if (debug_text_file) {
      exitfn_add(fclose_exitfn, debug_text_file);
    } else {
      cc_log("Failed to open %s: %s", path, strerror(errno));
    }
    free(path);
  }

  struct hash* common_hash = hash_init();
  init_hash_debug(common_hash,
                  ctx.args_info.output_obj.c_str(),
                  'c',
                  "COMMON",
                  debug_text_file);

  MTR_BEGIN("hash", "common_hash");
  hash_common_info(ctx, preprocessor_args, common_hash);
  MTR_END("hash", "common_hash");

  // Try to find the hash using the manifest.
  struct hash* direct_hash = hash_copy(common_hash);
  init_hash_debug(direct_hash,
                  ctx.args_info.output_obj.c_str(),
                  'd',
                  "DIRECT MODE",
                  debug_text_file);

  struct args* args_to_hash = args_copy(preprocessor_args);
  args_extend(args_to_hash, extra_args_to_hash);

  bool put_result_in_manifest = false;
  struct digest* result_name = NULL;
  struct digest* result_name_from_manifest = NULL;
  if (g_config.direct_mode()) {
    cc_log("Trying direct lookup");
    MTR_BEGIN("hash", "direct_hash");
    result_name =
      calculate_result_name(ctx, args_to_hash, nullptr, direct_hash, true);
    MTR_END("hash", "direct_hash");
    if (result_name) {
      update_cached_result_globals(ctx, result_name);

      // If we can return from cache at this point then do so.
      from_cache(ctx, FROMCACHE_DIRECT_MODE, 0);

      // Wasn't able to return from cache at this point. However, the result
      // was already found in manifest, so don't re-add it later.
      put_result_in_manifest = false;

      result_name_from_manifest = result_name;
    } else {
      // Add result to manifest later.
      put_result_in_manifest = true;
    }
  }

  if (g_config.read_only_direct()) {
    cc_log("Read-only direct mode; running real compiler");
    stats_update(ctx, STATS_CACHEMISS);
    failed(ctx);
  }

  if (!g_config.depend_mode()) {
    // Find the hash using the preprocessed output. Also updates
    // g_included_files.
    struct hash* cpp_hash = hash_copy(common_hash);
    init_hash_debug(cpp_hash,
                    ctx.args_info.output_obj.c_str(),
                    'p',
                    "PREPROCESSOR MODE",
                    debug_text_file);

    MTR_BEGIN("hash", "cpp_hash");
    result_name = calculate_result_name(
      ctx, args_to_hash, preprocessor_args, cpp_hash, false);
    MTR_END("hash", "cpp_hash");
    if (!result_name) {
      fatal("internal error: calculate_result_name returned NULL for cpp");
    }
    update_cached_result_globals(ctx, result_name);

    if (result_name_from_manifest
        && !digests_equal(result_name_from_manifest, result_name)) {
      // The hash from manifest differs from the hash of the preprocessor
      // output. This could be because:
      //
      // - The preprocessor produces different output for the same input (not
      //   likely).
      // - There's a bug in ccache (maybe incorrect handling of compiler
      //   arguments).
      // - The user has used a different CCACHE_BASEDIR (most likely).
      //
      // The best thing here would probably be to remove the hash entry from
      // the manifest. For now, we use a simpler method: just remove the
      // manifest file.
      cc_log("Hash from manifest doesn't match preprocessor output");
      cc_log("Likely reason: different CCACHE_BASEDIRs used");
      cc_log("Removing manifest as a safety measure");
      x_unlink(manifest_path);

      put_result_in_manifest = true;
    }

    // If we can return from cache at this point then do.
    from_cache(ctx, FROMCACHE_CPP_MODE, put_result_in_manifest);
  }

  if (g_config.read_only()) {
    cc_log("Read-only mode; running real compiler");
    stats_update(ctx, STATS_CACHEMISS);
    failed(ctx);
  }

  add_prefix(compiler_args, g_config.prefix_command().c_str());

  // In depend_mode, extend the direct hash.
  struct hash* depend_mode_hash = g_config.depend_mode() ? direct_hash : NULL;

  // Run real compiler, sending output to cache.
  MTR_BEGIN("cache", "to_cache");
  to_cache(
    ctx, compiler_args, ctx.args_info.depend_extra_args, depend_mode_hash);
  MTR_END("cache", "to_cache");

  x_exit(0);
}

// The main program when not doing a compile.
static int
ccache_main_options(Context& ctx, int argc, char* argv[])
{
  enum longopts {
    DUMP_MANIFEST,
    DUMP_RESULT,
    HASH_FILE,
    PRINT_STATS,
  };
  static const struct option options[] = {
    {"cleanup", no_argument, 0, 'c'},
    {"clear", no_argument, 0, 'C'},
    {"dump-manifest", required_argument, 0, DUMP_MANIFEST},
    {"dump-result", required_argument, 0, DUMP_RESULT},
    {"get-config", required_argument, 0, 'k'},
    {"hash-file", required_argument, 0, HASH_FILE},
    {"help", no_argument, 0, 'h'},
    {"max-files", required_argument, 0, 'F'},
    {"max-size", required_argument, 0, 'M'},
    {"print-stats", no_argument, 0, PRINT_STATS},
    {"recompress", required_argument, 0, 'X'},
    {"set-config", required_argument, 0, 'o'},
    {"show-compression", no_argument, 0, 'x'},
    {"show-config", no_argument, 0, 'p'},
    {"show-stats", no_argument, 0, 's'},
    {"version", no_argument, 0, 'V'},
    {"zero-stats", no_argument, 0, 'z'},
    {0, 0, 0, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "cCk:hF:M:po:sVxX:z", options, NULL))
         != -1) {
    switch (c) {
    case DUMP_MANIFEST:
      initialize(ctx.config);
      return manifest_dump(optarg, stdout) ? 0 : 1;

    case DUMP_RESULT:
      initialize(ctx.config);
      return result_dump(optarg, stdout) ? 0 : 1;

    case HASH_FILE: {
      initialize(ctx.config);
      struct hash* hash = hash_init();
      if (str_eq(optarg, "-")) {
        hash_fd(hash, STDIN_FILENO);
      } else {
        hash_file(hash, optarg);
      }
      char digest[DIGEST_STRING_BUFFER_SIZE];
      hash_result_as_string(hash, digest);
      puts(digest);
      hash_free(hash);
      break;
    }

    case PRINT_STATS:
      initialize(ctx.config);
      stats_print(ctx);
      break;

    case 'c': // --cleanup
    {
      initialize(ctx.config);
      ProgressBar progress_bar("Cleaning...");
      clean_up_all(g_config,
                   [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        printf("\n");
      }
      break;
    }

    case 'C': // --clear
    {
      initialize(ctx.config);
      ProgressBar progress_bar("Clearing...");
      wipe_all(g_config,
               [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        printf("\n");
      }
      break;
    }

    case 'h': // --help
      fputs(USAGE_TEXT, stdout);
      x_exit(0);

    case 'k': // --get-config
      initialize(ctx.config);
      fmt::print("{}\n", g_config.get_string_value(optarg));
      break;

    case 'F': { // --max-files
      initialize(ctx.config);
      g_config.set_value_in_file(
        g_config.primary_config_path(), "max_files", optarg);
      unsigned files = atoi(optarg);
      if (files == 0) {
        printf("Unset cache file limit\n");
      } else {
        printf("Set cache file limit to %u\n", files);
      }
      break;
    }

    case 'M': { // --max-size
      initialize(ctx.config);
      uint64_t size;
      if (!parse_size_with_suffix(optarg, &size)) {
        fatal("invalid size: %s", optarg);
      }
      g_config.set_value_in_file(
        g_config.primary_config_path(), "max_size", optarg);
      if (size == 0) {
        printf("Unset cache size limit\n");
      } else {
        char* s = format_human_readable_size(size);
        printf("Set cache size limit to %s\n", s);
        free(s);
      }
      break;
    }

    case 'o': { // --set-config
      initialize(ctx.config);
      char* p = strchr(optarg, '=');
      if (!p) {
        fatal("missing equal sign in \"%s\"", optarg);
      }
      char* key = x_strndup(optarg, p - optarg);
      char* value = p + 1;
      g_config.set_value_in_file(g_config.primary_config_path(), key, value);
      free(key);
      break;
    }

    case 'p': // --show-config
      initialize(ctx.config);
      g_config.visit_items(configuration_printer);
      break;

    case 's': // --show-stats
      initialize(ctx.config);
      stats_summary(ctx);
      break;

    case 'V': // --version
      fprintf(stdout, VERSION_TEXT, CCACHE_VERSION);
      x_exit(0);

    case 'x': // --show-compression
    {
      initialize(ctx.config);
      ProgressBar progress_bar("Scanning...");
      compress_stats(g_config,
                     [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'X': // --recompress
    {
      initialize(ctx.config);
      int level;
      if (std::string(optarg) == "uncompressed") {
        level = 0;
      } else {
        level = Util::parse_int(optarg);
        if (level < -128 || level > 127) {
          throw Error("compression level must be between -128 and 127");
        }
        if (level == 0) {
          level = g_config.compression_level();
        }
      }

      ProgressBar progress_bar("Recompressing...");
      compress_recompress(
        ctx, level, [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'z': // --zero-stats
      initialize(ctx.config);
      stats_zero(ctx);
      printf("Statistics zeroed\n");
      break;

    default:
      fputs(USAGE_TEXT, stderr);
      x_exit(1);
    }
  }

  return 0;
}

int ccache_main(int argc, char* argv[]);

int
ccache_main(int argc, char* argv[])
{
  try {
    // TODO ptr, and LEAK to be available in atexit()
    Context* ctx = new Context{};

    // Check if we are being invoked as "ccache".
    std::string program_name(Util::base_name(argv[0]));
    if (same_executable_name(program_name.c_str(), MYNAME)) {
      if (argc < 2) {
        fputs(USAGE_TEXT, stderr);
        x_exit(1);
      }
      // If the first argument isn't an option, then assume we are being passed
      // a compiler name and options.
      if (argv[1][0] == '-') {
        return ccache_main_options(*ctx, argc, argv);
      }
    }

    ccache(*ctx, argc, argv);
  } catch (const Error& e) {
    fmt::print("ccache: error: {}\n", e.what());
    return 1;
  }
}
