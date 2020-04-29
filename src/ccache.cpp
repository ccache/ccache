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

#include "Args.hpp"
#include "ArgsInfo.hpp"
#include "Context.hpp"
#include "File.hpp"
#include "FormatNonstdStringView.hpp"
#include "ProgressBar.hpp"
#include "ScopeGuard.hpp"
#include "Util.hpp"
#include "cleanup.hpp"
#include "compopt.hpp"
#include "compress.hpp"
#include "exceptions.hpp"
#include "execute.hpp"
#include "exitfn.hpp"
#include "hash.hpp"
#include "hashutil.hpp"
#include "language.hpp"
#include "logging.hpp"
#include "manifest.hpp"
#include "result.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"
#include "third_party/minitrace.h"
#include "third_party/nonstd/string_view.hpp"

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#else
#  include "third_party/getopt_long.h"
#endif

#include <limits>

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

using nonstd::nullopt;
using nonstd::optional;
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
  "    -C, --clear               clear the cache completely (except"
  " configuration)\n"
  "    -F, --max-files NUM       set maximum number of files in cache to NUM"
  " (use 0\n"
  "                              for no limit)\n"
  "    -M, --max-size SIZE       set maximum size of cache to SIZE (use 0 "
  "for no\n"
  "                              limit); available suffixes: k, M, G, T "
  "(decimal)\n"
  "                              and Ki, Mi, Gi, Ti (binary); default "
  "suffix: G\n"
  "    -X, --recompress LEVEL    recompress the cache to LEVEL (integer level"
  " or\n"
  "                              \"uncompressed\")\n"
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
  "        --dump-manifest PATH  dump manifest file at PATH in text format\n"
  "    -k, --get-config KEY      print the value of configuration key KEY\n"
  "        --hash-file PATH      print the hash (160 bit BLAKE2b) of the "
  "file at\n"
  "                              PATH\n"
  "        --print-stats         print statistics counter IDs and "
  "corresponding\n"
  "                              values in machine-parsable format\n"
  "    -o, --set-config KEY=VAL  set configuration item KEY to value VAL\n"
  "\n"
  "See also <https://ccache.dev>.\n";

enum fromcache_call_mode { FROMCACHE_DIRECT_MODE, FROMCACHE_CPP_MODE };

struct pending_tmp_file
{
  char* path;
  struct pending_tmp_file* next;
};

// Temporary files to remove at program exit.
static struct pending_tmp_file* pending_tmp_files = nullptr;

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
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
add_prefix(const Context& ctx, Args& args, const std::string& prefix_command)
{
  if (prefix_command.empty()) {
    return;
  }

  Args prefix;
  for (const auto& word : Util::split_into_strings(prefix_command, " ")) {
    std::string path = find_executable(ctx, word.c_str(), MYNAME);
    if (path.empty()) {
      fatal("%s: %s", word.c_str(), strerror(errno));
    }

    prefix.push_back(path);
  }

  cc_log("Using command-line prefix %s", prefix_command.c_str());
  for (size_t i = prefix.size(); i != 0; i--) {
    args_add_prefix(args, prefix->argv[i - 1]);
  }
}

// If `exit_code` is set, just exit with that code directly, otherwise execute
// the real compiler and exit with its exit code. Also updates statistics
// counter `stat` if it's not STATS_NONE.
static void failed(enum stats stat = STATS_NONE,
                   optional<int> exit_code = nullopt) ATTR_NORETURN;

static void
failed(enum stats stat, optional<int> exit_code)
{
  throw Failure(stat, exit_code);
}

static const char*
temp_dir(const Context& ctx)
{
  static const char* path = nullptr;
  if (path) {
    return path; // Memoize
  }
  path = ctx.config.temporary_dir().c_str();
  if (str_eq(path, "")) {
    path = format("%s/tmp", ctx.config.cache_dir().c_str());
  }
  return path;
}

void
block_signals()
{
#ifndef _WIN32
  sigprocmask(SIG_BLOCK, &fatal_signal_set, nullptr);
#endif
}

void
unblock_signals()
{
#ifndef _WIN32
  sigset_t empty;
  sigemptyset(&empty);
  sigprocmask(SIG_SETMASK, &empty, nullptr);
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
clean_up_pending_tmp_files()
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
      && waitpid(compiler_pid, nullptr, WNOHANG) == 0) {
    kill(compiler_pid, signum);
  }

  do_clean_up_pending_tmp_files();

  if (compiler_pid != 0) {
    // Wait for compiler subprocess to exit before we snuff it.
    waitpid(compiler_pid, nullptr, 0);
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
  sigaction(signum, &act, nullptr);
}

static void
set_up_signal_handlers()
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
clean_up_internal_tempdir(const Context& ctx)
{
  time_t now = time(nullptr);
  auto st = Stat::stat(ctx.config.cache_dir(), Stat::OnError::log);
  if (!st || st.mtime() + k_tempdir_cleanup_interval >= now) {
    // No cleanup needed.
    return;
  }

  update_mtime(ctx.config.cache_dir().c_str());

  DIR* dir = opendir(temp_dir(ctx));
  if (!dir) {
    return;
  }

  struct dirent* entry;
  while ((entry = readdir(dir))) {
    if (str_eq(entry->d_name, ".") || str_eq(entry->d_name, "..")) {
      continue;
    }

    char* path = format("%s/%s", temp_dir(ctx), entry->d_name);
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
  Context& ctx = *static_cast<Context*>(context);
  if (!ctx.config.debug()) {
    return;
  }

  std::string path = fmt::format("{}.ccache-log", ctx.args_info.output_obj);
  cc_dump_debug_log_buffer(path.c_str());
}

static void
init_hash_debug(const Context& ctx,
                struct hash* hash,
                const char* obj_path,
                char type,
                const char* section_name,
                FILE* debug_text_file)
{
  if (!ctx.config.debug()) {
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

static GuessedCompiler
guess_compiler(const char* path)
{
  string_view name = Util::base_name(path);
  GuessedCompiler result = GuessedCompiler::unknown;
  if (name == "clang") {
    result = GuessedCompiler::clang;
  } else if (name == "gcc" || name == "g++") {
    result = GuessedCompiler::gcc;
  } else if (name == "nvcc") {
    result = GuessedCompiler::nvcc;
  } else if (name == "pump" || name == "distcc-pump") {
    result = GuessedCompiler::pump;
  }
  return result;
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

  if (system && (ctx.config.sloppiness() & SLOPPY_SYSTEM_HEADERS)) {
    // Don't remember this system header.
    return true;
  }

  if (ctx.included_files.find(path) != ctx.included_files.end()) {
    // Already known include file.
    return true;
  }

  // Canonicalize path for comparison; Clang uses ./header.h.
  if (Util::starts_with(path, "./")) {
    path.erase(0, 2);
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

  for (const auto& ignore_header_path : ctx.ignore_header_paths) {
    if (Util::matches_dir_prefix_or_file(ignore_header_path, path)) {
      return true;
    }
  }

  // The comparison using >= is intentional, due to a possible race between
  // starting compilation and writing the include file. See also the notes
  // under "Performance" in doc/MANUAL.adoc.
  if (!(ctx.config.sloppiness() & SLOPPY_INCLUDE_FILE_MTIME)
      && st.mtime() >= ctx.time_of_compilation) {
    cc_log("Include file %s too new", path.c_str());
    return false;
  }

  // The same >= logic as above applies to the change time of the file.
  if (!(ctx.config.sloppiness() & SLOPPY_INCLUDE_FILE_CTIME)
      && st.ctime() >= ctx.time_of_compilation) {
    cc_log("Include file %s ctime too new", path.c_str());
    return false;
  }

  // Let's hash the include file content.
  std::unique_ptr<struct hash, decltype(&hash_free)> fhash_holder(hash_init(),
                                                                  &hash_free);
  fhash = fhash_holder.get();

  is_pch = is_precompiled_header(path.c_str());
  if (is_pch) {
    if (ctx.included_pch_file.empty()) {
      cc_log("Detected use of precompiled header: %s", path.c_str());
    }
    bool using_pch_sum = false;
    if (ctx.config.pch_external_checksum()) {
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

  if (ctx.config.direct_mode()) {
    if (!is_pch) { // else: the file has already been hashed.
      char* source = nullptr;
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
        hash_source_code_string(ctx.config, fhash, source, size, path.c_str());
      free(source);
      if (result & HASH_SOURCE_CODE_ERROR
          || result & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }
    }

    digest d;
    hash_result_as_bytes(fhash, &d);
    ctx.included_files.emplace(path, d);

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
      && ctx.config.direct_mode()) {
    cc_log("Disabling direct mode");
    ctx.config.set_direct_mode(false);
  }
}

static void
print_included_files(const Context& ctx, FILE* fp)
{
  for (const auto& item : ctx.included_files) {
    fprintf(fp, "%s\n", item.first.c_str());
  }
}

// Make a relative path from current working directory to `path` if `path` is
// under the base directory.
static std::string
make_relative_path(const Context& ctx, string_view path)
{
  if (ctx.config.base_dir().empty()
      || !Util::starts_with(path, ctx.config.base_dir())) {
    return std::string(path);
  }

#ifdef _WIN32
  std::string winpath;
  if (path.length() >= 3 && path[0] == '/') {
    if (isalpha(path[1]) && path[2] == '/') {
      // Transform /c/path... to c:/path...
      winpath = fmt::format("{}:/{}", path[1], path.substr(3));
      path = winpath;
    } else if (path[2] == ':') {
      // Transform /c:/path to c:/path
      winpath = std::string(path.substr(1));
      path = winpath;
    }
  }
#endif

  // The algorithm for computing relative paths below only works for existing
  // paths. If the path doesn't exist, find the first ancestor directory that
  // does exist and assemble the path again afterwards.
  string_view original_path = path;
  std::string path_suffix;
  Stat path_stat;
  while (!(path_stat = Stat::stat(std::string(path)))) {
    path = Util::dir_name(path);
  }
  path_suffix = std::string(original_path.substr(path.length()));

  std::string path_str(path);
  std::string normalized_path = Util::normalize_absolute_path(path_str);
  std::vector<std::string> relpath_candidates = {
    Util::get_relative_path(ctx.actual_cwd, normalized_path),
    Util::get_relative_path(ctx.apparent_cwd, normalized_path),
  };
  // Move best (= shortest) match first:
  if (relpath_candidates[0].length() > relpath_candidates[1].length()) {
    std::swap(relpath_candidates[0], relpath_candidates[1]);
  }

  for (const auto& relpath : relpath_candidates) {
    if (Stat::stat(relpath).same_inode_as(path_stat)) {
      return relpath + path_suffix;
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return std::string(original_path);
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
      if (!ctx.has_absolute_include_headers) {
        ctx.has_absolute_include_headers = Util::is_absolute_path(inc_path);
      }
      char* saved_inc_path = inc_path;
      inc_path = x_strdup(make_relative_path(ctx, inc_path).c_str());
      free(saved_inc_path);

      bool should_hash_inc_path = true;
      if (!ctx.config.hash_dir()) {
        if (Util::starts_with(inc_path, ctx.apparent_cwd)
            && str_endswith(inc_path, "//")) {
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

      remember_include_file(ctx, inc_path, hash, system, nullptr);
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
      failed(STATS_UNSUPPORTED_DIRECTIVE);
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

  // Explicitly check the .gch/.pch/.pth file as Clang does not include any
  // mention of it in the preprocessed output.
  if (!ctx.included_pch_file.empty()) {
    std::string pch_path =
      make_relative_path(ctx, ctx.included_pch_file.c_str());
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
use_relative_paths_in_depfile(const Context& ctx)
{
  if (ctx.config.base_dir().empty()) {
    cc_log("Base dir not set, skip using relative paths");
    return; // nothing to do
  }
  if (!ctx.has_absolute_include_headers) {
    cc_log(
      "No absolute path for included files found, skip using relative paths");
    return; // nothing to do
  }

  const std::string& output_dep = ctx.args_info.output_dep;
  std::string file_content;
  try {
    file_content = Util::read_file(output_dep);
  } catch (const Error& e) {
    cc_log("Cannot open dependency file %s: %s", output_dep.c_str(), e.what());
    return;
  }

  std::string adjusted_file_content;
  adjusted_file_content.reserve(file_content.size());

  bool rewritten = false;

  for (string_view token : Util::split_into_views(file_content, " \t\r\n")) {
    if (Util::is_absolute_path(token)
        && token.starts_with(ctx.config.base_dir())) {
      adjusted_file_content.append(make_relative_path(ctx, token));
      rewritten = true;
    } else {
      adjusted_file_content.append(token.begin(), token.end());
    }
    adjusted_file_content.push_back(' ');
  }

  if (!rewritten) {
    cc_log(
      "No paths in dependency file %s made relative, skip relative path usage",
      output_dep.c_str());
    return;
  }

  std::string tmp_file = fmt::format("{}.tmp{}", output_dep, tmp_string());

  try {
    Util::write_file(tmp_file, adjusted_file_content);
  } catch (const Error& e) {
    cc_log(
      "Error writing temporary dependency file %s (%s), skip relative path"
      " usage",
      tmp_file.c_str(),
      e.what());
    x_unlink(tmp_file.c_str());
    return;
  }

  if (x_rename(tmp_file.c_str(), output_dep.c_str()) != 0) {
    cc_log(
      "Error renaming dependency file: %s -> %s (%s), skip relative path usage",
      tmp_file.c_str(),
      output_dep.c_str(),
      strerror(errno));
    x_unlink(tmp_file.c_str());
  } else {
    cc_log("Renamed dependency file: %s -> %s",
           tmp_file.c_str(),
           output_dep.c_str());
  }
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static struct digest*
result_name_from_depfile(Context& ctx, struct hash* hash)
{
  std::string file_content;
  try {
    file_content = Util::read_file(ctx.args_info.output_dep);
  } catch (const Error& e) {
    cc_log("Cannot open dependency file %s: %s",
           ctx.args_info.output_dep.c_str(),
           e.what());
    return nullptr;
  }

  for (string_view token : Util::split_into_views(file_content, " \t\r\n")) {
    if (token == "\\" || token.ends_with(":")) {
      continue;
    }
    if (!ctx.has_absolute_include_headers) {
      ctx.has_absolute_include_headers = Util::is_absolute_path(token);
    }
    std::string path = make_relative_path(ctx, token);
    remember_include_file(ctx, path, hash, false, hash);
  }

  // Explicitly check the .gch/.pch/.pth file as it may not be mentioned in the
  // dependencies output.
  if (!ctx.included_pch_file.empty()) {
    std::string pch_path =
      make_relative_path(ctx, ctx.included_pch_file.c_str());
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
  if (!ctx.config.direct_mode() || ctx.config.read_only()
      || ctx.config.read_only_direct()) {
    return;
  }

  auto old_st = Stat::stat(ctx.manifest_path());

  // See comment in get_file_hash_index for why saving of timestamps is forced
  // for precompiled headers.
  bool save_timestamp = (ctx.config.sloppiness() & SLOPPY_FILE_STAT_MATCHES)
                        || ctx.args_info.output_is_precompiled_header;

  MTR_BEGIN("manifest", "manifest_put");
  cc_log("Adding result name to %s", ctx.manifest_path().c_str());
  if (!manifest_put(ctx.config,
                    ctx.manifest_path(),
                    ctx.result_name(),
                    ctx.included_files,
                    ctx.time_of_compilation,
                    save_timestamp)) {
    cc_log("Failed to add result name to %s", ctx.manifest_path().c_str());
  } else {
    auto st = Stat::stat(ctx.manifest_path(), Stat::OnError::log);

    int64_t size_delta = st.size_on_disk() - old_st.size_on_disk();
    int nof_files_delta = !old_st && st ? 1 : 0;

    if (ctx.stats_file() == ctx.manifest_stats_file()) {
      stats_update_size(ctx.counter_updates, size_delta, nof_files_delta);
    } else {
      Counters counters;
      stats_update_size(counters, size_delta, nof_files_delta);
      stats_flush_to_file(ctx.config, ctx.manifest_stats_file(), counters);
    }
  }
  MTR_END("manifest", "manifest_put");
}

static bool
create_cachedir_tag(nonstd::string_view dir)
{
  constexpr char cachedir_tag[] =
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

  File f(filename, "w");

  if (!f) {
    return false;
  }

  return fwrite(cachedir_tag, strlen(cachedir_tag), 1, f.get()) == 1;
}

// Run the real compiler and put the result in cache.
static void
to_cache(Context& ctx,
         Args& args,
         Args& depend_extra_args,
         struct hash* depend_mode_hash)
{
  args_add(args, "-o");
  args.push_back(ctx.args_info.output_obj);

  if (ctx.config.hard_link() && ctx.args_info.output_obj != "/dev/null") {
    // Workaround for Clang bug where it overwrites an existing object file
    // when it's compiling an assembler file, see
    // <https://bugs.llvm.org/show_bug.cgi?id=39782>.
    x_unlink(ctx.args_info.output_obj.c_str());
  }

  if (ctx.args_info.generating_diagnostics) {
    args_add(args, "--serialize-diagnostics");
    args.push_back(ctx.args_info.output_dia);
  }

  // Turn off DEPENDENCIES_OUTPUT when running cc1, because otherwise it will
  // emit a line like this:
  //
  //   tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
  x_unsetenv("DEPENDENCIES_OUTPUT");
  x_unsetenv("SUNPRO_DEPENDENCIES");

  if (ctx.config.run_second_cpp()) {
    args.push_back(ctx.args_info.input_file);
  } else {
    args.push_back(ctx.i_tmpfile);
  }

  if (ctx.args_info.seen_split_dwarf) {
    // Remove any pre-existing .dwo file since we want to check if the compiler
    // produced one, intentionally not using x_unlink or tmp_unlink since we're
    // not interested in logging successful deletions or failures due to
    // non-existent .dwo files.
    if (unlink(ctx.args_info.output_dwo.c_str()) == -1 && errno != ENOENT) {
      cc_log("Failed to unlink %s: %s",
             ctx.args_info.output_dwo.c_str(),
             strerror(errno));
      failed(STATS_BADOUTPUTFILE);
    }
  }

  cc_log("Running real compiler");
  MTR_BEGIN("execute", "compiler");
  char* tmp_stdout;
  int tmp_stdout_fd;
  char* tmp_stderr;
  int tmp_stderr_fd;
  int status;
  if (!ctx.config.depend_mode()) {
    tmp_stdout = format("%s/tmp.stdout", temp_dir(ctx));
    tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
    tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx));
    tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
    status = execute(
      args.to_argv().data(), tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
    args_pop(args, 3);
  } else {
    // The cached result path is not known yet, use temporary files.
    tmp_stdout = format("%s/tmp.stdout", temp_dir(ctx));
    tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
    tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx));
    tmp_stderr_fd = create_tmp_fd(&tmp_stderr);

    // Use the original arguments (including dependency options) in depend
    // mode.
    Args depend_mode_args = ctx.orig_args;
    args_strip(depend_mode_args, "--ccache-");
    args_extend(depend_mode_args, depend_extra_args);
    add_prefix(ctx, depend_mode_args, ctx.config.prefix_command());

    ctx.time_of_compilation = time(nullptr);
    status = execute(depend_mode_args.to_argv().data(),
                     tmp_stdout_fd,
                     tmp_stderr_fd,
                     &compiler_pid);
  }
  MTR_END("execute", "compiler");

  auto st = Stat::stat(tmp_stdout, Stat::OnError::log);
  if (!st) {
    // The stdout file was removed - cleanup in progress? Better bail out.
    tmp_unlink(tmp_stdout);
    tmp_unlink(tmp_stderr);
    failed(STATS_MISSING);
  }

  // distcc-pump outputs lines like this:
  // __________Using # distcc servers in pump mode
  if (st.size() != 0 && ctx.guessed_compiler != GuessedCompiler::pump) {
    cc_log("Compiler produced stdout");
    tmp_unlink(tmp_stdout);
    tmp_unlink(tmp_stderr);
    failed(STATS_STDOUT);
  }
  tmp_unlink(tmp_stdout);

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler into tmp_stderr.
  if (!ctx.cpp_stderr.empty()) {
    char* tmp_stderr2 = format("%s.2", tmp_stderr);
    if (x_rename(tmp_stderr, tmp_stderr2)) {
      cc_log("Failed to rename %s to %s: %s",
             tmp_stderr,
             tmp_stderr2,
             strerror(errno));
      failed(STATS_ERROR);
    }

    int fd_cpp_stderr = open(ctx.cpp_stderr.c_str(), O_RDONLY | O_BINARY);
    if (fd_cpp_stderr == -1) {
      cc_log("Failed opening %s: %s", ctx.cpp_stderr.c_str(), strerror(errno));
      failed(STATS_ERROR);
    }

    int fd_real_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
    if (fd_real_stderr == -1) {
      cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
      failed(STATS_ERROR);
    }

    int fd_result =
      open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd_result == -1) {
      cc_log("Failed opening %s: %s", tmp_stderr, strerror(errno));
      failed(STATS_ERROR);
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

    int fd = open(tmp_stderr, O_RDONLY | O_BINARY);
    if (fd != -1) {
      // We can output stderr immediately instead of rerunning the compiler.
      copy_fd(fd, 2);
      close(fd);
      tmp_unlink(tmp_stderr);

      failed(STATS_STATUS, status);
    }

    tmp_unlink(tmp_stderr);
    failed(STATS_STATUS);
  }

  if (ctx.config.depend_mode()) {
    struct digest* result_name =
      result_name_from_depfile(ctx, depend_mode_hash);
    if (!result_name) {
      failed(STATS_ERROR);
    }
    ctx.set_result_name(*result_name);
  }

  bool produce_dep_file = ctx.args_info.generating_dependencies
                          && ctx.args_info.output_dep != "/dev/null";

  if (produce_dep_file) {
    use_relative_paths_in_depfile(ctx);
  }

  st = Stat::stat(ctx.args_info.output_obj);
  if (!st) {
    cc_log("Compiler didn't produce an object file");
    failed(STATS_NOOUTPUT);
  }
  if (st.size() == 0) {
    cc_log("Compiler produced an empty object file");
    failed(STATS_EMPTYOUTPUT);
  }

  st = Stat::stat(tmp_stderr, Stat::OnError::log);
  if (!st) {
    failed(STATS_ERROR);
  }
  ResultFileMap result_file_map;
  if (st.size() > 0) {
    result_file_map.emplace(FileType::stderr_output, tmp_stderr);
  }
  result_file_map.emplace(FileType::object, ctx.args_info.output_obj);
  if (ctx.args_info.generating_dependencies) {
    result_file_map.emplace(FileType::dependency, ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    result_file_map.emplace(FileType::coverage, ctx.args_info.output_cov);
  }
  if (ctx.args_info.generating_stackusage) {
    result_file_map.emplace(FileType::stackusage, ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    result_file_map.emplace(FileType::diagnostic, ctx.args_info.output_dia);
  }
  if (ctx.args_info.seen_split_dwarf && Stat::stat(ctx.args_info.output_dwo)) {
    // Only copy .dwo file if it was created by the compiler (GCC and Clang
    // behave differently e.g. for "-gsplit-dwarf -g1").
    result_file_map.emplace(FileType::dwarf_object, ctx.args_info.output_dwo);
  }

  auto orig_dest_stat = Stat::stat(ctx.result_path());
  result_put(ctx, ctx.result_path(), result_file_map);

  cc_log("Stored in cache: %s", ctx.result_path().c_str());

  auto new_dest_stat = Stat::stat(ctx.result_path(), Stat::OnError::log);
  if (!new_dest_stat) {
    failed(STATS_ERROR);
  }
  stats_update_size(ctx.counter_updates,
                    new_dest_stat.size_on_disk()
                      - orig_dest_stat.size_on_disk(),
                    orig_dest_stat ? 0 : 1);

  MTR_END("file", "file_put");

  // Make sure we have a CACHEDIR.TAG in the cache part of cache_dir. This can
  // be done almost anywhere, but we might as well do it near the end as we
  // save the stat call if we exit early.
  {
    std::string first_level_dir(Util::dir_name(ctx.stats_file()));
    if (!create_cachedir_tag(first_level_dir)) {
      cc_log("Failed to create %s/CACHEDIR.TAG (%s)",
             first_level_dir.c_str(),
             strerror(errno));
    }

    // Remove any CACHEDIR.TAG on the cache_dir level where it was located in
    // previous ccache versions.
    if (getpid() % 1000 == 0) {
      char* path = format("%s/CACHEDIR.TAG", ctx.config.cache_dir().c_str());
      x_unlink(path);
      free(path);
    }
  }

  // Everything OK.
  send_cached_stderr(tmp_stderr);
  tmp_unlink(tmp_stderr);

  free(tmp_stderr);
  free(tmp_stdout);
}

// Find the result name by running the compiler in preprocessor mode and
// hashing the result.
static struct digest*
get_result_name_from_cpp(Context& ctx, Args& args, struct hash* hash)
{
  ctx.time_of_compilation = time(nullptr);

  char* path_stderr = nullptr;
  char* path_stdout = nullptr;
  int status;
  if (ctx.args_info.direct_i_file) {
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
    path_stdout =
      x_strdup(fmt::format("{}/{}.stdout", temp_dir(ctx), input_base).c_str());
    int path_stdout_fd = create_tmp_fd(&path_stdout);
    add_pending_tmp_file(path_stdout);

    path_stderr = format("%s/tmp.cpp_stderr", temp_dir(ctx));
    int path_stderr_fd = create_tmp_fd(&path_stderr);
    add_pending_tmp_file(path_stderr);

    int args_added = 2;
    args_add(args, "-E");
    if (ctx.config.keep_comments_cpp()) {
      args_add(args, "-C");
      args_added = 3;
    }
    args.push_back(ctx.args_info.input_file);
    add_prefix(ctx, args, ctx.config.prefix_command_cpp());
    cc_log("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    status = execute(
      args.to_argv().data(), path_stdout_fd, path_stderr_fd, &compiler_pid);
    MTR_END("execute", "preprocessor");
    args_pop(args, args_added);
  }

  if (status != 0) {
    cc_log("Preprocessor gave exit status %d", status);
    failed(STATS_PREPROCESSOR);
  }

  hash_delimiter(hash, "cpp");
  if (!process_preprocessed_file(ctx,
                                 hash,
                                 path_stdout,
                                 ctx.guessed_compiler
                                   == GuessedCompiler::pump)) {
    failed(STATS_ERROR);
  }

  hash_delimiter(hash, "cppstderr");
  if (!ctx.args_info.direct_i_file && !hash_file(hash, path_stderr)) {
    // Somebody removed the temporary file?
    cc_log("Failed to open %s: %s", path_stderr, strerror(errno));
    failed(STATS_ERROR);
  }

  if (ctx.args_info.direct_i_file) {
    ctx.i_tmpfile = ctx.args_info.input_file;
  } else {
    // i_tmpfile needs the proper cpp_extension for the compiler to do its
    // thing correctly
    ctx.i_tmpfile =
      fmt::format("{}.{}", path_stdout, ctx.config.cpp_extension());
    x_rename(path_stdout, ctx.i_tmpfile.c_str());
    add_pending_tmp_file(ctx.i_tmpfile.c_str());
  }

  if (ctx.config.run_second_cpp()) {
    free(path_stderr);
  } else {
    // If we are using the CPP trick, we need to remember this stderr data and
    // output it just before the main stderr from the compiler pass.
    ctx.cpp_stderr = from_cstr(path_stderr);
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
  if (ctx.config.compiler_check() == "none") {
    // Do nothing.
  } else if (ctx.config.compiler_check() == "mtime") {
    hash_delimiter(hash, "cc_mtime");
    hash_int(hash, st.size());
    hash_int(hash, st.mtime());
  } else if (Util::starts_with(ctx.config.compiler_check(), "string:")) {
    hash_delimiter(hash, "cc_hash");
    hash_string(hash, ctx.config.compiler_check().c_str() + strlen("string:"));
  } else if (ctx.config.compiler_check() == "content" || !allow_command) {
    hash_delimiter(hash, "cc_content");
    hash_file(hash, path);
  } else { // command string
    if (!hash_multicommand_output(
          hash, ctx.config.compiler_check().c_str(), ctx.orig_args->argv[0])) {
      cc_log("Failure running compiler check command: %s",
             ctx.config.compiler_check().c_str());
      failed(STATS_COMPCHECK);
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
    for (const char* compiler : compilers) {
      if (ccbin) {
        char* path = format("%s/%s", ccbin, compiler);
        auto st = Stat::stat(path);
        if (st) {
          hash_compiler(ctx, hash, st, path, false);
        }
        free(path);
      } else {
        std::string path = find_executable(ctx, compiler, MYNAME);
        if (!path.empty()) {
          auto st = Stat::stat(path, Stat::OnError::log);
          hash_compiler(ctx, hash, st, ccbin, false);
        }
      }
    }
  } else {
    hash_compiler(ctx, hash, *ccbin_st, ccbin, false);
  }
}

// Update a hash with information common for the direct and preprocessor modes.
static void
hash_common_info(const Context& ctx,
                 const Args& args,
                 struct hash* hash,
                 const ArgsInfo& args_info)
{
  hash_string(hash, HASH_PREFIX);

  // We have to hash the extension, as a .i file isn't treated the same by the
  // compiler as a .ii file.
  hash_delimiter(hash, "ext");
  hash_string(hash, ctx.config.cpp_extension().c_str());

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
    failed(STATS_COMPILER);
  }

  // Hash information about the compiler.
  hash_compiler(ctx, hash, st, args->argv[0], true);

  // Also hash the compiler name as some compilers use hard links and behave
  // differently depending on the real name.
  hash_delimiter(hash, "cc_name");
  string_view base = Util::base_name(args->argv[0]);
  hash_string_view(hash, base);

  if (!(ctx.config.sloppiness() & SLOPPY_LOCALE)) {
    // Hash environment variables that may affect localization of compiler
    // warning messages.
    const char* envvars[] = {
      "LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", nullptr};
    for (const char** p = envvars; *p; ++p) {
      char* v = getenv(*p);
      if (v) {
        hash_delimiter(hash, *p);
        hash_string(hash, v);
      }
    }
  }

  // Possibly hash the current working directory.
  if (args_info.generating_debuginfo && ctx.config.hash_dir()) {
    std::string dir_to_hash = ctx.apparent_cwd;
    for (size_t i = 0; i < args_info.debug_prefix_maps_len; i++) {
      char* map = args_info.debug_prefix_maps[i];
      char* sep = strchr(map, '=');
      if (sep) {
        char* old_path = x_strndup(map, sep - map);
        char* new_path = static_cast<char*>(x_strdup(sep + 1));
        cc_log("Relocating debuginfo from %s to %s (CWD: %s)",
               old_path,
               new_path,
               ctx.apparent_cwd.c_str());
        if (Util::starts_with(ctx.apparent_cwd, old_path)) {
          dir_to_hash = new_path + ctx.apparent_cwd.substr(strlen(old_path));
        }
        free(old_path);
        free(new_path);
      }
    }
    cc_log("Hashing CWD %s", dir_to_hash.c_str());
    hash_delimiter(hash, "cwd");
    hash_string(hash, dir_to_hash);
  }

  if (ctx.args_info.generating_dependencies || ctx.args_info.seen_split_dwarf) {
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
  if (ctx.args_info.generating_coverage && ctx.args_info.profile_arcs) {
    std::string dir;
    if (!ctx.args_info.profile_dir.empty()) {
      dir = ctx.args_info.profile_dir;
    } else {
      dir =
        Util::real_path(std::string(Util::dir_name(ctx.args_info.output_obj)));
    }
    string_view stem =
      Util::remove_extension(Util::base_name(ctx.args_info.output_obj));
    std::string gcda_path = fmt::format("{}/{}.gcda", dir, stem);
    cc_log("Hashing coverage path %s", gcda_path.c_str());
    hash_delimiter(hash, "gcda");
    hash_string(hash, gcda_path);
  }

  // Possibly hash the sanitize blacklist file path.
  for (size_t i = 0; i < args_info.sanitize_blacklists_len; i++) {
    char* sanitize_blacklist = args_info.sanitize_blacklists[i];
    cc_log("Hashing sanitize blacklist %s", sanitize_blacklist);
    hash_delimiter(hash, "sanitizeblacklist");
    if (!hash_file(hash, sanitize_blacklist)) {
      failed(STATS_BADEXTRAFILE);
    }
  }

  if (!ctx.config.extra_files_to_hash().empty()) {
    for (const std::string& path : Util::split_into_strings(
           ctx.config.extra_files_to_hash(), PATH_DELIM)) {
      cc_log("Hashing extra file %s", path.c_str());
      hash_delimiter(hash, "extrafile");
      if (!hash_file(hash, path.c_str())) {
        failed(STATS_BADEXTRAFILE);
      }
    }
  }

  // Possibly hash GCC_COLORS (for color diagnostics).
  if (ctx.guessed_compiler == GuessedCompiler::gcc) {
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
                      const Args& args,
                      Args& preprocessor_args,
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
  int is_clang = ctx.guessed_compiler == GuessedCompiler::clang
                 || ctx.guessed_compiler == GuessedCompiler::unknown;

  // First the arguments.
  for (size_t i = 1; i < args.size(); i++) {
    // -L doesn't affect compilation (except for clang).
    if (i < args.size() - 1 && str_eq(args->argv[i], "-L") && !is_clang) {
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
    if (!direct_mode && !ctx.args_info.output_is_precompiled_header
        && !ctx.args_info.using_precompiled_header) {
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
    if (ctx.args_info.generating_dependencies) {
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

    const char* p = nullptr;
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

    if (str_eq(args->argv[i], "-Xclang") && i + 3 < args.size()
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
        && i + 1 < args.size()) {
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
    if (i + 1 < args.size() && compopt_takes_arg(args->argv[i])) {
      i++;
      hash_delimiter(hash, "arg");
      hash_string(hash, args->argv[i]);
    }
  }

  // Make results with dependency file /dev/null different from those without
  // it.
  if (ctx.args_info.generating_dependencies
      && ctx.args_info.output_dep == "/dev/null") {
    hash_delimiter(hash, "/dev/null dependency file");
  }

  if (!found_ccbin && ctx.args_info.actual_language == "cu") {
    hash_nvcc_host_compiler(ctx, hash, nullptr, nullptr);
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
  if (ctx.args_info.profile_generate) {
    cc_log("Adding profile directory %s to our hash",
           ctx.args_info.profile_dir.c_str());
    hash_delimiter(hash, "-fprofile-dir");
    hash_string(hash, ctx.args_info.profile_dir);
  }

  if (ctx.args_info.profile_use) {
    // Calculate gcda name.
    string_view base_name = Util::remove_extension(ctx.args_info.output_obj);
    std::string gcda_name =
      fmt::format("{}/{}.gcda", ctx.args_info.profile_dir, base_name);
    cc_log("Adding profile data %s to our hash", gcda_name.c_str());
    // Add the gcda to our hash.
    hash_delimiter(hash, "-fprofile-use");
    hash_file(hash, gcda_name.c_str());
  }

  // Adding -arch to hash since cpp output is affected.
  for (size_t i = 0; i < ctx.args_info.arch_args_size; ++i) {
    hash_delimiter(hash, "-arch");
    hash_string(hash, ctx.args_info.arch_args[i]);
  }

  struct digest* result_name = nullptr;
  if (direct_mode) {
    // Hash environment variables that affect the preprocessor output.
    const char* envvars[] = {"CPATH",
                             "C_INCLUDE_PATH",
                             "CPLUS_INCLUDE_PATH",
                             "OBJC_INCLUDE_PATH",
                             "OBJCPLUS_INCLUDE_PATH", // clang
                             nullptr};
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
      hash_source_code_file(ctx.config, hash, ctx.args_info.input_file.c_str());
    if (result & HASH_SOURCE_CODE_ERROR) {
      failed(STATS_ERROR);
    }
    if (result & HASH_SOURCE_CODE_FOUND_TIME) {
      cc_log("Disabling direct mode");
      ctx.config.set_direct_mode(false);
      return nullptr;
    }

    struct digest manifest_name;
    hash_result_as_bytes(hash, &manifest_name);
    ctx.set_manifest_name(manifest_name);

    cc_log("Looking for result name in %s", ctx.manifest_path().c_str());
    MTR_BEGIN("manifest", "manifest_get");
    result_name = manifest_get(ctx, ctx.manifest_path());
    MTR_END("manifest", "manifest_get");
    if (result_name) {
      cc_log("Got result name from manifest");
    } else {
      cc_log("Did not find result name in manifest");
    }
  } else {
    if (ctx.args_info.arch_args_size == 0) {
      result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
      cc_log("Got result name from preprocessor");
    } else {
      args_add(preprocessor_args, "-arch");
      for (size_t i = 0; i < ctx.args_info.arch_args_size; ++i) {
        args_add(preprocessor_args, ctx.args_info.arch_args[i]);
        result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
        cc_log("Got result name from preprocessor with -arch %s",
               ctx.args_info.arch_args[i]);
        if (i != ctx.args_info.arch_args_size - 1) {
          free(result_name);
          result_name = nullptr;
        }
        args_pop(preprocessor_args, 1);
      }
      args_pop(preprocessor_args, 1);
    }
  }

  return result_name;
}

// Try to return the compile result from cache.
static optional<enum stats>
from_cache(Context& ctx, enum fromcache_call_mode mode)
{
  // The user might be disabling cache hits.
  if (ctx.config.recache()) {
    return nullopt;
  }

  // If we're using Clang, we can't trust a precompiled header object based on
  // running the preprocessor since clang will produce a fatal error when the
  // precompiled header is used and one of the included files has an updated
  // timestamp:
  //
  //     file 'foo.h' has been modified since the precompiled header 'foo.pch'
  //     was built
  if ((ctx.guessed_compiler == GuessedCompiler::clang
       || ctx.guessed_compiler == GuessedCompiler::unknown)
      && ctx.args_info.output_is_precompiled_header
      && mode == FROMCACHE_CPP_MODE) {
    cc_log("Not considering cached precompiled header in preprocessor mode");
    return nullopt;
  }

  MTR_BEGIN("cache", "from_cache");

  bool produce_dep_file = ctx.args_info.generating_dependencies
                          && ctx.args_info.output_dep != "/dev/null";

  MTR_BEGIN("file", "file_get");

  // Get result from cache.
  char* tmp_stderr = format("%s/tmp.stderr", temp_dir(ctx));
  int tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
  close(tmp_stderr_fd);

  ResultFileMap result_file_map;
  if (ctx.args_info.output_obj != "/dev/null") {
    result_file_map.emplace(FileType::object, ctx.args_info.output_obj);
    if (ctx.args_info.seen_split_dwarf) {
      result_file_map.emplace(FileType::dwarf_object, ctx.args_info.output_dwo);
    }
  }
  result_file_map.emplace(FileType::stderr_output, tmp_stderr);
  if (produce_dep_file) {
    result_file_map.emplace(FileType::dependency, ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    result_file_map.emplace(FileType::coverage, ctx.args_info.output_cov);
  }
  if (ctx.args_info.generating_stackusage) {
    result_file_map.emplace(FileType::stackusage, ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    result_file_map.emplace(FileType::diagnostic, ctx.args_info.output_dia);
  }
  bool ok = result_get(ctx, ctx.result_path(), result_file_map);
  if (!ok) {
    cc_log("Failed to get result from cache");
    tmp_unlink(tmp_stderr);
    free(tmp_stderr);
    return nullopt;
  }

  MTR_END("file", "file_get");

  send_cached_stderr(tmp_stderr);

  tmp_unlink(tmp_stderr);
  free(tmp_stderr);

  cc_log("Succeeded getting cached result");

  MTR_END("cache", "from_cache");

  return mode == FROMCACHE_DIRECT_MODE ? STATS_CACHEHIT_DIR
                                       : STATS_CACHEHIT_CPP;
}

// Find the real compiler. We just search the PATH to find an executable of the
// same name that isn't a link to ourselves.
static void
find_compiler(Context& ctx, const char* const* argv)
{
  // We might be being invoked like "ccache gcc -c foo.c".
  std::string base(Util::base_name(argv[0]));
  if (same_executable_name(base.c_str(), MYNAME)) {
    args_remove_first(ctx.orig_args);
    if (is_full_path(ctx.orig_args->argv[0])) {
      // A full path was given.
      return;
    }
    base = std::string(Util::base_name(ctx.orig_args->argv[0]));
  }

  // Support user override of the compiler.
  if (!ctx.config.compiler().empty()) {
    base = ctx.config.compiler();
  }

  std::string compiler = find_executable(ctx, base.c_str(), MYNAME);
  if (compiler.empty()) {
    fatal("Could not find compiler \"%s\" in PATH", base.c_str());
  }
  if (compiler == argv[0]) {
    fatal("Recursive invocation (the name of the ccache binary must be \"%s\")",
          MYNAME);
  }
  ctx.orig_args[0] = compiler;
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
  char* pch_file = nullptr;
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
    if (!ctx.included_pch_file.empty()) {
      cc_log("Multiple precompiled headers used: %s and %s\n",
             ctx.included_pch_file.c_str(),
             pch_file);
      return false;
    }
    ctx.included_pch_file = pch_file;
    *found_pch = true;
  }
  return true;
}

// Process the compiler options into options suitable for passing to the
// preprocessor and the real compiler. preprocessor_args doesn't include -E;
// this is added later. extra_args_to_hash are the arguments that are not
// included in preprocessor_args but that should be included in the hash.
//
// Returns nullopt on success, otherwise the statistics counter that should be
// incremented.
optional<enum stats>
process_args(Context& ctx,
             const Args& args,
             Args& preprocessor_args,
             Args& extra_args_to_hash,
             Args& compiler_args)
{
  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

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
  Args expanded_args = args;

  // common_args contains all original arguments except:
  // * those that never should be passed to the preprocessor,
  // * those that only should be passed to the preprocessor (if run_second_cpp
  //   is false), and
  // * dependency options (like -MD and friends).
  Args common_args;

  // cpp_args contains arguments that were not added to common_args, i.e. those
  // that should only be passed to the preprocessor if run_second_cpp is false.
  // If run_second_cpp is true, they will be passed to the compiler as well.
  Args cpp_args;

  // dep_args contains dependency options like -MD. They are only passed to the
  // preprocessor, never to the compiler.
  Args dep_args;

  // compiler_only_args contains arguments that should only be passed to the
  // compiler, not the preprocessor.
  Args compiler_only_args;

  bool found_color_diagnostics = false;
  bool found_directives_only = false;
  bool found_rewrite_includes = false;

  // Compiler in depend mode is invoked with the original arguments.
  // Collect extra arguments that should be added.
  auto add_extra_arg = [&args_info, &config](const char* arg) {
    if (config.depend_mode()) {
      args_add(args_info.depend_extra_args, arg);
    }
  };

  auto& argv = expanded_args->argv;
  args_add(common_args, argv[0]);

  for (size_t i = 1; i < expanded_args.size(); i++) {
    size_t argc = expanded_args.size();

    // The user knows best: just swallow the next arg.
    if (str_eq(argv[i], "--ccache-skip")) {
      i++;
      if (i == argc) {
        cc_log("--ccache-skip lacks an argument");
        return STATS_ARGS;
      }
      args_add(common_args, argv[i]);
      continue;
    }

    // Special case for -E.
    if (str_eq(argv[i], "-E")) {
      return STATS_PREPROCESSING;
    }

    // Handle "@file" argument.
    if (str_startswith(argv[i], "@") || str_startswith(argv[i], "-@")) {
      const char* argpath = argv[i] + 1;

      if (argpath[-1] == '-') {
        ++argpath;
      }
      auto file_args = args_init_from_gcc_atfile(argpath);
      if (!file_args) {
        cc_log("Couldn't read arg file %s", argpath);
        return STATS_ARGS;
      }

      args_insert(expanded_args, i, *file_args, true);
      i--;
      continue;
    }

    // Handle cuda "-optf" and "--options-file" argument.
    if (ctx.guessed_compiler == GuessedCompiler::nvcc
        && (str_eq(argv[i], "-optf") || str_eq(argv[i], "--options-file"))) {
      if (i == argc - 1) {
        cc_log("Expected argument after %s", argv[i]);
        return STATS_ARGS;
      }
      ++i;

      // Argument is a comma-separated list of files.
      const char* str_start = argv[i];
      const char* str_end = strchr(str_start, ',');
      size_t index = i + 1;

      if (!str_end) {
        str_end = str_start + strlen(str_start);
      }

      while (str_end) {
        std::string path(str_start, str_end - str_start);
        auto file_args = args_init_from_gcc_atfile(path);
        if (!file_args) {
          cc_log("Couldn't read cuda options file %s", path.c_str());
          return STATS_ARGS;
        }

        size_t new_index = file_args->size() + index;
        args_insert(expanded_args, index, *file_args, false);
        index = new_index;
        str_start = str_end;
        str_end = strchr(str_start, ',');
      }

      continue;
    }

    // These are always too hard.
    if (compopt_too_hard(argv[i]) || str_startswith(argv[i], "-fdump-")
        || str_startswith(argv[i], "-MJ")) {
      cc_log("Compiler option %s is unsupported", argv[i]);
      return STATS_UNSUPPORTED_OPTION;
    }

    // These are too hard in direct mode.
    if (config.direct_mode() && compopt_too_hard_for_direct_mode(argv[i])) {
      cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
      config.set_direct_mode(false);
    }

    // -Xarch_* options are too hard.
    if (str_startswith(argv[i], "-Xarch_")) {
      cc_log("Unsupported compiler option :%s", argv[i]);
      return STATS_UNSUPPORTED_OPTION;
    }

    // Handle -arch options.
    if (str_eq(argv[i], "-arch")) {
      if (args_info.arch_args_size == ArgsInfo::max_arch_args - 1) {
        cc_log("Too many -arch compiler options; ccache supports at most %d",
               ArgsInfo::max_arch_args);
        return STATS_UNSUPPORTED_OPTION;
      }

      ++i;
      args_info.arch_args[args_info.arch_args_size] =
        x_strdup(argv[i]); // It will leak.
      ++args_info.arch_args_size;
      if (args_info.arch_args_size == 2) {
        config.set_run_second_cpp(true);
      }
      continue;
    }

    // Handle options that should not be passed to the preprocessor.
    if (compopt_affects_comp(argv[i])) {
      args_add(compiler_only_args, argv[i]);
      if (compopt_takes_arg(argv[i])) {
        if (i == argc - 1) {
          cc_log("Missing argument to %s", argv[i]);
          return STATS_ARGS;
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
      if (!config.depend_mode() || !config.direct_mode()) {
        cc_log("Compiler option %s is unsupported without direct depend mode",
               argv[i]);
        return STATS_CANTUSEMODULES;
      } else if (!(config.sloppiness() & SLOPPY_MODULES)) {
        cc_log(
          "You have to specify \"modules\" sloppiness when using"
          " -fmodules to get hits");
        return STATS_CANTUSEMODULES;
      }
    }

    // We must have -c.
    if (str_eq(argv[i], "-c")) {
      found_c_opt = true;
      continue;
    }

    // when using nvcc with separable compilation, -dc implies -c
    if ((str_eq(argv[i], "-dc") || str_eq(argv[i], "--device-c"))
        && ctx.guessed_compiler == GuessedCompiler::nvcc) {
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
        return STATS_ARGS;
      }
      if (args_info.input_file.empty()) {
        explicit_language = argv[i + 1];
      }
      i++;
      continue;
    }
    if (str_startswith(argv[i], "-x")) {
      if (args_info.input_file.empty()) {
        if (str_eq(&argv[i][2], "Host")) {
            // Skip -xHost for Intel compiler support
            continue;
        }
        explicit_language = &argv[i][2];
      }
      continue;
    }

    // We need to work out where the output was meant to go.
    if (str_eq(argv[i], "-o")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        return STATS_ARGS;
      }
      args_info.output_obj = make_relative_path(ctx, argv[i + 1]);
      i++;
      continue;
    }

    // Alternate form of -o with no space. Nvcc does not support this.
    if (str_startswith(argv[i], "-o")
        && ctx.guessed_compiler != GuessedCompiler::nvcc) {
      args_info.output_obj = make_relative_path(ctx, &argv[i][2]);
      continue;
    }

    if (str_startswith(argv[i], "-fdebug-prefix-map=")
        || str_startswith(argv[i], "-ffile-prefix-map=")) {
      args_info.debug_prefix_maps = static_cast<char**>(
        x_realloc(args_info.debug_prefix_maps,
                  (args_info.debug_prefix_maps_len + 1) * sizeof(char*)));
      args_info.debug_prefix_maps[args_info.debug_prefix_maps_len++] =
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
        args_info.generating_debuginfo = true;
        continue;
      }

      if (str_startswith(argv[i], "-gz")) {
        // -gz[=type] neither disables nor enables debug info.
        continue;
      }

      char last_char = argv[i][strlen(argv[i]) - 1];
      if (last_char == '0') {
        // "-g0", "-ggdb0" or similar: All debug information disabled.
        args_info.generating_debuginfo = false;
        generating_debuginfo_level_3 = false;
      } else {
        args_info.generating_debuginfo = true;
        if (last_char == '3') {
          generating_debuginfo_level_3 = true;
        }
        if (str_eq(argv[i], "-gsplit-dwarf")) {
          args_info.seen_split_dwarf = true;
        }
      }
      continue;
    }

    // These options require special handling, because they behave differently
    // with gcc -E, when the output file is not specified.
    if (str_eq(argv[i], "-MD") || str_eq(argv[i], "-MMD")) {
      args_info.generating_dependencies = true;
      args_add(dep_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-MF")) {
      dependency_filename_specified = true;

      const char* arg;
      bool separate_argument = (strlen(argv[i]) == 3);
      if (separate_argument) {
        // -MF arg
        if (i == argc - 1) {
          cc_log("Missing argument to %s", argv[i]);
          return STATS_ARGS;
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
      args_info.output_dep = make_relative_path(ctx, arg);
      // Keep the format of the args the same.
      if (separate_argument) {
        args_add(dep_args, "-MF");
        dep_args.push_back(args_info.output_dep);
      } else {
        char* option = format("-MF%s", args_info.output_dep.c_str());
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
          return STATS_ARGS;
        }
        args_add(dep_args, argv[i]);
        std::string relpath = make_relative_path(ctx, argv[i + 1]);
        dep_args.push_back(relpath);
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
      args_info.profile_arcs = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "-ftest-coverage")) {
      args_info.generating_coverage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "-fstack-usage")) {
      args_info.generating_stackusage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_eq(argv[i], "--coverage")      // = -fprofile-arcs -ftest-coverage
        || str_eq(argv[i], "-coverage")) { // Undocumented but still works.
      args_info.profile_arcs = true;
      args_info.generating_coverage = true;
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-fprofile-dir=")) {
      args_info.profile_dir.assign(argv[i] + 14);
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "-fsanitize-blacklist=")) {
      args_info.sanitize_blacklists = static_cast<char**>(
        x_realloc(args_info.sanitize_blacklists,
                  (args_info.sanitize_blacklists_len + 1) * sizeof(char*)));
      args_info.sanitize_blacklists[args_info.sanitize_blacklists_len++] =
        x_strdup(argv[i] + 21);
      args_add(common_args, argv[i]);
      continue;
    }
    if (str_startswith(argv[i], "--sysroot=")) {
      std::string relpath = make_relative_path(ctx, argv[i] + 10);
      std::string option = fmt::format("--sysroot={}", relpath);
      common_args.push_back(option);
      continue;
    }
    // Alternate form of specifying sysroot without =
    if (str_eq(argv[i], "--sysroot")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        return STATS_ARGS;
      }
      args_add(common_args, argv[i]);
      std::string relpath = make_relative_path(ctx, argv[i + 1]);
      common_args.push_back(relpath);
      i++;
      continue;
    }
    // Alternate form of specifying target without =
    if (str_eq(argv[i], "-target")) {
      if (i == argc - 1) {
        cc_log("Missing argument to %s", argv[i]);
        return STATS_ARGS;
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
        return STATS_UNSUPPORTED_OPTION;
      } else if (str_startswith(argv[i], "-Wp,-MD,")
                 && !strchr(argv[i] + 8, ',')) {
        args_info.generating_dependencies = true;
        dependency_filename_specified = true;
        args_info.output_dep = make_relative_path(ctx, argv[i] + 8);
        args_add(dep_args, argv[i]);
        continue;
      } else if (str_startswith(argv[i], "-Wp,-MMD,")
                 && !strchr(argv[i] + 9, ',')) {
        args_info.generating_dependencies = true;
        dependency_filename_specified = true;
        args_info.output_dep = make_relative_path(ctx, argv[i] + 9);
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
      } else if (config.direct_mode()) {
        // -Wp, can be used to pass too hard options to the preprocessor.
        // Hence, disable direct mode.
        cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
        config.set_direct_mode(false);
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
        return STATS_ARGS;
      }
      args_info.generating_diagnostics = true;
      args_info.output_dia = make_relative_path(ctx, argv[i + 1]);
      i++;
      continue;
    }

    if (str_startswith(argv[i], "-fprofile-")) {
      char* arg = x_strdup(argv[i]);
      const char* arg_profile_dir = strchr(argv[i], '=');
      if (arg_profile_dir) {
        // Convert to absolute path.
        std::string dir = Util::real_path(arg_profile_dir + 1);

        // We can get a better hit rate by using the real path here.
        free(arg);
        char* option = x_strndup(argv[i], arg_profile_dir - argv[i]);
        arg = format("%s=%s", option, dir.c_str());
        cc_log("Rewriting %s to %s", argv[i], arg);
        free(option);
      }

      bool supported_profile_option = false;
      if (str_startswith(argv[i], "-fprofile-generate")
          || str_eq(argv[i], "-fprofile-arcs")) {
        args_info.profile_generate = true;
        supported_profile_option = true;
      } else if (str_startswith(argv[i], "-fprofile-use")
                 || str_eq(argv[i], "-fbranch-probabilities")) {
        args_info.profile_use = true;
        supported_profile_option = true;
      } else if (str_eq(argv[i], "-fprofile-dir")) {
        supported_profile_option = true;
      }

      if (supported_profile_option) {
        args_add(common_args, arg);
        free(arg);

        // If the profile directory has already been set, give up... Hard to
        // know what the user means, and what the compiler will do.
        if (arg_profile_dir && !args_info.profile_dir.empty()) {
          cc_log("Profile directory already set; giving up");
          return STATS_UNSUPPORTED_OPTION;
        } else if (arg_profile_dir) {
          cc_log("Setting profile directory to %s", arg_profile_dir);
          args_info.profile_dir = from_cstr(arg_profile_dir);
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

    if (config.sloppiness() & SLOPPY_CLANG_INDEX_STORE
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
        return STATS_ARGS;
      }

      if (!detect_pch(ctx, argv[i], argv[i + 1], &found_pch)) {
        return STATS_ARGS;
      }

      std::string relpath = make_relative_path(ctx, argv[i + 1]);
      if (compopt_affects_cpp(argv[i])) {
        args_add(cpp_args, argv[i]);
        cpp_args.push_back(relpath);
      } else {
        args_add(common_args, argv[i]);
        common_args.push_back(relpath);
      }

      i++;
      continue;
    }

    // Same as above but options with concatenated argument beginning with a
    // slash.
    if (argv[i][0] == '-') {
      const char* slash_pos = strchr(argv[i], '/');
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
        return STATS_ARGS;
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

    if (!args_info.input_file.empty()) {
      if (language_for_file(argv[i])) {
        cc_log("Multiple input files: %s and %s",
               args_info.input_file.c_str(),
               argv[i]);
        return STATS_MULTIPLE;
      } else if (!found_c_opt && !found_dc_opt) {
        cc_log("Called for link with %s", argv[i]);
        if (strstr(argv[i], "conftest.")) {
          return STATS_CONFTEST;
        } else {
          return STATS_LINK;
        }
      } else {
        cc_log("Unsupported source extension: %s", argv[i]);
        return STATS_SOURCELANG;
      }
    }

    // The source code file path gets put into the notes.
    if (args_info.generating_coverage) {
      args_info.input_file = from_cstr(argv[i]);
      continue;
    }

    // Rewrite to relative to increase hit rate.
    args_info.input_file = make_relative_path(ctx, argv[i]);
  } // for

  if (generating_debuginfo_level_3 && !config.run_second_cpp()) {
    cc_log("Generating debug info level 3; not compiling preprocessed code");
    config.set_run_second_cpp(true);
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
      args_info.generating_dependencies = true;
      dependency_filename_specified = true;

      auto dependencies = Util::split_into_views(dependencies_env, " ");

      if (!dependencies.empty()) {
        auto abspath_file = dependencies.at(0);
        args_info.output_dep = make_relative_path(ctx, abspath_file);
      }

      // specifying target object is optional.
      if (dependencies.size() > 1) {
        // it's the "file target" form.
        string_view abspath_obj = dependencies.at(1);

        dependency_target_specified = true;
        std::string relpath_obj = make_relative_path(ctx, abspath_obj);
        // ensure compiler gets relative path.
        std::string relpath_both =
          fmt::format("{} {}", args_info.output_dep, relpath_obj);
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
          x_setenv("SUNPRO_DEPENDENCIES", args_info.output_dep.c_str());
        } else {
          x_setenv("DEPENDENCIES_OUTPUT", args_info.output_dep.c_str());
        }
      }
    }
  }

  if (args_info.input_file.empty()) {
    cc_log("No input file found");
    return STATS_NOINPUT;
  }

  if (found_pch || found_fpch_preprocess) {
    args_info.using_precompiled_header = true;
    if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
      cc_log(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      cc_log("Disabling direct mode");
      return STATS_CANTUSEPCH;
    }
  }

  if (args_info.profile_dir.empty()) {
    args_info.profile_dir = ctx.apparent_cwd;
  }

  if (explicit_language && str_eq(explicit_language, "none")) {
    explicit_language = nullptr;
  }
  file_language = language_for_file(args_info.input_file.c_str());
  if (explicit_language) {
    if (!language_is_supported(explicit_language)) {
      cc_log("Unsupported language: %s", explicit_language);
      return STATS_SOURCELANG;
    }
    args_info.actual_language = from_cstr(explicit_language);
  } else {
    args_info.actual_language = from_cstr(file_language);
  }

  args_info.output_is_precompiled_header =
    args_info.actual_language.find("-header") != std::string::npos;

  if (args_info.output_is_precompiled_header
      && !(config.sloppiness() & SLOPPY_PCH_DEFINES)) {
    cc_log(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    return STATS_CANTUSEPCH;
  }

  if (!found_c_opt && !found_dc_opt && !found_S_opt) {
    if (args_info.output_is_precompiled_header) {
      args_add(common_args, "-c");
    } else {
      cc_log("No -c option found");
      // I find that having a separate statistic for autoconf tests is useful,
      // as they are the dominant form of "called for link" in many cases.
      if (args_info.input_file.find("conftest.") != std::string::npos) {
        return STATS_CONFTEST;
      } else {
        return STATS_LINK;
      }
    }
  }

  if (args_info.actual_language.empty()) {
    cc_log("Unsupported source extension: %s", args_info.input_file.c_str());
    return STATS_SOURCELANG;
  }

  if (!config.run_second_cpp() && args_info.actual_language == "cu") {
    cc_log("Using CUDA compiler; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  args_info.direct_i_file =
    language_is_preprocessed(args_info.actual_language.c_str());

  if (args_info.output_is_precompiled_header && !config.run_second_cpp()) {
    // It doesn't work to create the .gch from preprocessed source.
    cc_log("Creating precompiled header; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  if (config.cpp_extension().empty()) {
    const char* p_language =
      p_language_for_language(args_info.actual_language.c_str());
    config.set_cpp_extension(extension_for_language(p_language) + 1);
  }

  // Don't try to second guess the compilers heuristics for stdout handling.
  if (args_info.output_obj == "-") {
    cc_log("Output file is -");
    return STATS_OUTSTDOUT;
  }

  if (args_info.output_obj.empty()) {
    if (args_info.output_is_precompiled_header) {
      args_info.output_obj = args_info.input_file + ".gch";
    } else {
      string_view extension = found_S_opt ? ".s" : ".o";
      args_info.output_obj = Util::change_extension(
        Util::base_name(args_info.input_file), extension);
    }
  }

  if (args_info.seen_split_dwarf) {
    size_t pos = args_info.output_obj.rfind('.');
    if (pos == std::string::npos || pos == args_info.output_obj.size() - 1) {
      cc_log("Badly formed object filename");
      return STATS_ARGS;
    }

    args_info.output_dwo = Util::change_extension(args_info.output_obj, ".dwo");
  }

  // Cope with -o /dev/null.
  if (args_info.output_obj != "/dev/null") {
    auto st = Stat::stat(args_info.output_obj);
    if (st && !st.is_regular()) {
      cc_log("Not a regular file: %s", args_info.output_obj.c_str());
      return STATS_BADOUTPUTFILE;
    }
  }

  {
    char* output_dir = x_dirname(args_info.output_obj.c_str());
    auto st = Stat::stat(output_dir);
    if (!st || !st.is_directory()) {
      cc_log("Directory does not exist: %s", output_dir);
      free(output_dir);
      return STATS_BADOUTPUTFILE;
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
    if (ctx.guessed_compiler == GuessedCompiler::clang) {
      if (args_info.actual_language != "assembler") {
        args_add(common_args, "-fcolor-diagnostics");
        add_extra_arg("-fcolor-diagnostics");
        cc_log("Automatically enabling colors");
      }
    } else if (ctx.guessed_compiler == GuessedCompiler::gcc) {
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

  if (args_info.generating_dependencies) {
    if (!dependency_filename_specified) {
      std::string default_depfile_name =
        Util::change_extension(args_info.output_obj, ".d");
      args_info.output_dep = make_relative_path(ctx, default_depfile_name);
      if (!config.run_second_cpp()) {
        // If we're compiling preprocessed code we're sending dep_args to the
        // preprocessor so we need to use -MF to write to the correct .d file
        // location since the preprocessor doesn't know the final object path.
        args_add(dep_args, "-MF");
        dep_args.push_back(default_depfile_name);
      }
    }

    if (!dependency_target_specified && !dependency_implicit_target_specified
        && !config.run_second_cpp()) {
      // If we're compiling preprocessed code we're sending dep_args to the
      // preprocessor so we need to use -MQ to get the correct target object
      // file in the .d file.
      args_add(dep_args, "-MQ");
      dep_args.push_back(args_info.output_obj);
    }
  }
  if (args_info.generating_coverage) {
    std::string gcda_path =
      Util::change_extension(args_info.output_obj, ".gcno");
    args_info.output_cov = make_relative_path(ctx, gcda_path);
  }
  if (args_info.generating_stackusage) {
    std::string default_sufile_name =
      Util::change_extension(args_info.output_obj, ".su");
    args_info.output_su = make_relative_path(ctx, default_sufile_name);
  }

  *compiler_args = args_copy(common_args);
  args_extend(*compiler_args, compiler_only_args);

  if (config.run_second_cpp()) {
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
      compiler_args.push_back(args_info.actual_language);
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

  for (size_t i = 0; i < args_info.arch_args_size; ++i) {
    args_add(*compiler_args, "-arch");
    args_add(*compiler_args, args_info.arch_args[i]);
  }

  *preprocessor_args = args_copy(common_args);
  args_extend(*preprocessor_args, cpp_args);

  if (config.run_second_cpp()) {
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
  if (config.run_second_cpp()) {
    args_extend(*extra_args_to_hash, dep_args);
  }

  return nullopt;
}

static void
create_initial_config_file(Config& config)
{
  if (!Util::create_dir(Util::dir_name(config.primary_config_path()))) {
    return;
  }

  unsigned max_files;
  uint64_t max_size;
  char* stats_dir = format("%s/0", config.cache_dir().c_str());
  if (Stat::stat(stats_dir)) {
    stats_get_obsolete_limits(stats_dir, &max_files, &max_size);
    // STATS_MAXFILES and STATS_MAXSIZE was stored for each top directory.
    max_files *= 16;
    max_size *= 16;
  } else {
    max_files = 0;
    max_size = config.max_size();
  }
  free(stats_dir);

  FILE* f = fopen(config.primary_config_path().c_str(), "w");
  if (!f) {
    return;
  }
  if (max_files != 0) {
    fprintf(f, "max_files = %u\n", max_files);
    config.set_max_files(max_files);
  }
  if (max_size != 0) {
    char* size = format_parsable_size_with_suffix(max_size);
    fprintf(f, "max_size = %s\n", size);
    free(size);
    config.set_max_size(max_size);
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
trace_start()
{
  MTR_META_PROCESS_NAME(MYNAME);
  trace_id = (void*)((long)getpid());
  MTR_START("program", "ccache", trace_id);
}

static void
trace_stop()
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
set_up_config(Config& config)
{
  char* p = getenv("CCACHE_CONFIGPATH");
  if (p) {
    config.set_primary_config_path(p);
  } else {
    config.set_secondary_config_path(
      fmt::format("{}/ccache.conf", TO_STRING(SYSCONFDIR)));
    MTR_BEGIN("config", "conf_read_secondary");
    // A missing config file in SYSCONFDIR is OK so don't check return value.
    config.update_from_file(config.secondary_config_path());
    MTR_END("config", "conf_read_secondary");

    if (config.cache_dir().empty()) {
      fatal("configuration setting \"cache_dir\" must not be the empty string");
    }
    if ((p = getenv("CCACHE_DIR"))) {
      config.set_cache_dir(p);
    }
    if (config.cache_dir().empty()) {
      fatal("CCACHE_DIR must not be the empty string");
    }

    config.set_primary_config_path(
      fmt::format("{}/ccache.conf", config.cache_dir()));
  }

  bool should_create_initial_config = false;
  MTR_BEGIN("config", "conf_read_primary");
  if (!config.update_from_file(config.primary_config_path())
      && !config.disable()) {
    should_create_initial_config = true;
  }
  MTR_END("config", "conf_read_primary");

  MTR_BEGIN("config", "conf_update_from_environment");
  config.update_from_environment();
  MTR_END("config", "conf_update_from_environment");

  if (should_create_initial_config) {
    create_initial_config_file(config);
  }

  if (config.umask() != std::numeric_limits<uint32_t>::max()) {
    umask(config.umask());
  }
}

static void
set_up_context(Context& ctx, int argc, const char* const* argv)
{
  ctx.orig_args = args_init(argc, argv);
  ctx.ignore_header_paths = Util::split_into_strings(
    ctx.config.ignore_headers_in_manifest(), PATH_DELIM);
}

// Initialize ccache, must be called once before anything else is run.
static Context&
initialize(int argc, const char* const* argv)
{
  // This object is placed onto the heap so it is available in exit functions
  // which run after main(). It is cleaned up by the last exit function.
  Context* ctx = new Context;

  set_up_config(ctx->config);
  set_up_context(*ctx, argc, argv);
  init_log(ctx->config);

  exitfn_init();
  exitfn_delete_context(ctx);
  exitfn_add(stats_flush, ctx);
  exitfn_add_nullary(clean_up_pending_tmp_files);

  bool enable_internal_trace = getenv("CCACHE_INTERNAL_TRACE");
  if (enable_internal_trace) {
#ifdef MTR_ENABLED
    // We don't have any conf yet, so we can't use temp_dir() here.
    trace_init(format("%s/tmp.ccache-trace.%d", tmpdir(), (int)getpid()));
#endif
  }

  cc_log("=== CCACHE %s STARTED =========================================",
         CCACHE_VERSION);

  if (enable_internal_trace) {
#ifdef MTR_ENABLED
    trace_start();
    exitfn_add_nullary(trace_stop);
#else
    cc_log("Error: tracing is not enabled!");
#endif
  }

  return *ctx;
}

// Make a copy of stderr that will not be cached, so things like distcc can
// send networking errors to it.
static void
set_up_uncached_err()
{
  int uncached_fd = dup(2); // The file descriptor is intentionally leaked.
  if (uncached_fd == -1) {
    cc_log("dup(2) failed: %s", strerror(errno));
    failed(STATS_ERROR);
  }

  // Leak a pointer to the environment.
  char* buf = format("UNCACHED_ERR_FD=%d", uncached_fd);
  if (putenv(buf) == -1) {
    cc_log("putenv failed: %s", strerror(errno));
    failed(STATS_ERROR);
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

static int cache_compilation(int argc, const char* const* argv);
static enum stats do_cache_compilation(Context& ctx, const char* const* argv);

// The entry point when invoked to cache a compilation.
static int
cache_compilation(int argc, const char* const* argv)
{
#ifndef _WIN32
  set_up_signal_handlers();
#endif

  // Needed for portability when using localtime_r.
  tzset();

  Context& ctx = initialize(argc, argv);

  MTR_BEGIN("main", "find_compiler");
  find_compiler(ctx, argv);
  MTR_END("main", "find_compiler");

  try {
    enum stats stat = do_cache_compilation(ctx, argv);
    stats_update(ctx, stat);
    return EXIT_SUCCESS;
  } catch (const Failure& e) {
    if (e.stat() != STATS_NONE) {
      stats_update(ctx, e.stat());
    }

    if (e.exit_code()) {
      return *e.exit_code();
    }
    // Else: Fall back to running the real compiler.

    assert(ctx.orig_args.size() > 0);

    args_strip(ctx.orig_args, "--ccache-");
    add_prefix(ctx, ctx.orig_args, ctx.config.prefix_command());

    cc_log("Failed; falling back to running the real compiler");

    // exitfn_call deletes ctx and thereby ctx.orig_orgs, so save it.
    Args saved_orig_args(std::move(ctx.orig_args));
    auto execv_argv = saved_orig_args.to_argv();

    cc_log_argv("Executing ", execv_argv.data());
    exitfn_call();
    execv(execv_argv[0], const_cast<char* const*>(execv_argv.data()));
    fatal("execv of %s failed: %s", execv_argv[0], strerror(errno));
  }
}

static enum stats
do_cache_compilation(Context& ctx, const char* const* argv)
{
  if (ctx.actual_cwd.empty()) {
    cc_log("Unable to determine current working directory: %s",
           strerror(errno));
    failed(STATS_ERROR);
  }

  MTR_BEGIN("main", "clean_up_internal_tempdir");
  if (ctx.config.temporary_dir().empty()) {
    clean_up_internal_tempdir(ctx);
  }
  MTR_END("main", "clean_up_internal_tempdir");

  if (!ctx.config.log_file().empty() || ctx.config.debug()) {
    ctx.config.visit_items(configuration_logger);
  }

  if (ctx.config.disable()) {
    cc_log("ccache is disabled");
    // STATS_CACHEMISS is a dummy to trigger stats_flush.
    failed(STATS_CACHEMISS);
  }

  MTR_BEGIN("main", "set_up_uncached_err");
  set_up_uncached_err();
  MTR_END("main", "set_up_uncached_err");

  cc_log_argv("Command line: ", argv);
  cc_log("Hostname: %s", get_hostname());
  cc_log("Working directory: %s", ctx.actual_cwd.c_str());
  if (ctx.apparent_cwd != ctx.actual_cwd) {
    cc_log("Apparent working directory: %s", ctx.apparent_cwd.c_str());
  }

  ctx.config.set_limit_multiple(
    std::min(std::max(ctx.config.limit_multiple(), 0.0), 1.0));

  MTR_BEGIN("main", "guess_compiler");
  ctx.guessed_compiler = guess_compiler(ctx.orig_args->argv[0]);
  MTR_END("main", "guess_compiler");

  // Arguments (except -E) to send to the preprocessor.
  Args preprocessor_args;
  // Arguments not sent to the preprocessor but that should be part of the
  // hash.
  Args extra_args_to_hash;
  // Arguments to send to the real compiler.
  Args compiler_args;
  MTR_BEGIN("main", "process_args");

  auto error = process_args(
    ctx, ctx.orig_args, preprocessor_args, extra_args_to_hash, compiler_args);
  if (error) {
    failed(*error);
  }

  MTR_END("main", "process_args");

  if (ctx.config.depend_mode()
      && (!ctx.args_info.generating_dependencies
          || ctx.args_info.output_dep == "/dev/null"
          || !ctx.config.run_second_cpp())) {
    cc_log("Disabling depend mode");
    ctx.config.set_depend_mode(false);
  }

  cc_log("Source file: %s", ctx.args_info.input_file.c_str());
  if (ctx.args_info.generating_dependencies) {
    cc_log("Dependency file: %s", ctx.args_info.output_dep.c_str());
  }
  if (ctx.args_info.generating_coverage) {
    cc_log("Coverage file: %s", ctx.args_info.output_cov.c_str());
  }
  if (ctx.args_info.generating_stackusage) {
    cc_log("Stack usage file: %s", ctx.args_info.output_su.c_str());
  }
  if (ctx.args_info.generating_diagnostics) {
    cc_log("Diagnostics file: %s", ctx.args_info.output_dia.c_str());
  }
  if (!ctx.args_info.output_dwo.empty()) {
    cc_log("Split dwarf file: %s", ctx.args_info.output_dwo.c_str());
  }

  cc_log("Object file: %s", ctx.args_info.output_obj.c_str());
  MTR_META_THREAD_NAME(ctx.args_info.output_obj);

  // Need to dump log buffer as the last exit function to not lose any logs.
  exitfn_add_last(dump_debug_log_buffer_exitfn, &ctx);

  FILE* debug_text_file = nullptr;
  if (ctx.config.debug()) {
    std::string path =
      fmt::format("{}.ccache-input-text", ctx.args_info.output_obj);
    debug_text_file = fopen(path.c_str(), "w");
    if (debug_text_file) {
      exitfn_add(fclose_exitfn, debug_text_file);
    } else {
      cc_log("Failed to open %s: %s", path.c_str(), strerror(errno));
    }
  }

  struct hash* common_hash = hash_init();
  init_hash_debug(ctx,
                  common_hash,
                  ctx.args_info.output_obj.c_str(),
                  'c',
                  "COMMON",
                  debug_text_file);

  MTR_BEGIN("hash", "common_hash");
  hash_common_info(ctx, preprocessor_args, common_hash, ctx.args_info);
  MTR_END("hash", "common_hash");

  // Try to find the hash using the manifest.
  struct hash* direct_hash = hash_copy(common_hash);
  init_hash_debug(ctx,
                  direct_hash,
                  ctx.args_info.output_obj.c_str(),
                  'd',
                  "DIRECT MODE",
                  debug_text_file);

  Args args_to_hash = preprocessor_args;
  args_extend(args_to_hash, extra_args_to_hash);

  bool put_result_in_manifest = false;
  struct digest* result_name = nullptr;
  struct digest* result_name_from_manifest = nullptr;
  if (ctx.config.direct_mode()) {
    cc_log("Trying direct lookup");
    MTR_BEGIN("hash", "direct_hash");
    Args dummy_args;
    result_name =
      calculate_result_name(ctx, args_to_hash, dummy_args, direct_hash, true);
    MTR_END("hash", "direct_hash");
    if (result_name) {
      ctx.set_result_name(*result_name);

      // If we can return from cache at this point then do so.
      auto result = from_cache(ctx, FROMCACHE_DIRECT_MODE);
      if (result) {
        return *result;
      }

      // Wasn't able to return from cache at this point. However, the result
      // was already found in manifest, so don't re-add it later.
      put_result_in_manifest = false;

      result_name_from_manifest = result_name;
    } else {
      // Add result to manifest later.
      put_result_in_manifest = true;
    }
  }

  if (ctx.config.read_only_direct()) {
    cc_log("Read-only direct mode; running real compiler");
    failed(STATS_CACHEMISS);
  }

  if (!ctx.config.depend_mode()) {
    // Find the hash using the preprocessed output. Also updates
    // g_included_files.
    struct hash* cpp_hash = hash_copy(common_hash);
    init_hash_debug(ctx,
                    cpp_hash,
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
    ctx.set_result_name(*result_name);

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
      x_unlink(ctx.manifest_path().c_str());

      put_result_in_manifest = true;
    }

    // If we can return from cache at this point then do.
    auto result = from_cache(ctx, FROMCACHE_CPP_MODE);
    if (result) {
      if (put_result_in_manifest) {
        update_manifest_file(ctx);
      }
      return *result;
    }
  }

  if (ctx.config.read_only()) {
    cc_log("Read-only mode; running real compiler");
    failed(STATS_CACHEMISS);
  }

  add_prefix(ctx, compiler_args, ctx.config.prefix_command());

  // In depend_mode, extend the direct hash.
  struct hash* depend_mode_hash =
    ctx.config.depend_mode() ? direct_hash : nullptr;

  // Run real compiler, sending output to cache.
  MTR_BEGIN("cache", "to_cache");
  to_cache(
    ctx, compiler_args, ctx.args_info.depend_extra_args, depend_mode_hash);
  update_manifest_file(ctx);
  MTR_END("cache", "to_cache");

  return STATS_CACHEMISS;
}

// The main program when not doing a compile.
static int
handle_main_options(int argc, const char* const* argv)
{
  enum longopts {
    DUMP_MANIFEST,
    DUMP_RESULT,
    HASH_FILE,
    PRINT_STATS,
  };
  static const struct option options[] = {
    {"cleanup", no_argument, nullptr, 'c'},
    {"clear", no_argument, nullptr, 'C'},
    {"dump-manifest", required_argument, nullptr, DUMP_MANIFEST},
    {"dump-result", required_argument, nullptr, DUMP_RESULT},
    {"get-config", required_argument, nullptr, 'k'},
    {"hash-file", required_argument, nullptr, HASH_FILE},
    {"help", no_argument, nullptr, 'h'},
    {"max-files", required_argument, nullptr, 'F'},
    {"max-size", required_argument, nullptr, 'M'},
    {"print-stats", no_argument, nullptr, PRINT_STATS},
    {"recompress", required_argument, nullptr, 'X'},
    {"set-config", required_argument, nullptr, 'o'},
    {"show-compression", no_argument, nullptr, 'x'},
    {"show-config", no_argument, nullptr, 'p'},
    {"show-stats", no_argument, nullptr, 's'},
    {"version", no_argument, nullptr, 'V'},
    {"zero-stats", no_argument, nullptr, 'z'},
    {nullptr, 0, nullptr, 0}};

  Context& ctx = initialize(argc, argv);

  int c;
  while ((c = getopt_long(argc,
                          const_cast<char* const*>(argv),
                          "cCk:hF:M:po:sVxX:z",
                          options,
                          nullptr))
         != -1) {
    switch (c) {
    case DUMP_MANIFEST:
      return manifest_dump(optarg, stdout) ? 0 : 1;

    case DUMP_RESULT:
      return result_dump(ctx, optarg, stdout) ? 0 : 1;

    case HASH_FILE: {
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
      stats_print(ctx.config);
      break;

    case 'c': // --cleanup
    {
      ProgressBar progress_bar("Cleaning...");
      clean_up_all(ctx.config,
                   [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        printf("\n");
      }
      break;
    }

    case 'C': // --clear
    {
      ProgressBar progress_bar("Clearing...");
      wipe_all(ctx.config,
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
      fmt::print("{}\n", ctx.config.get_string_value(optarg));
      break;

    case 'F': { // --max-files
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_files", optarg);
      unsigned files = atoi(optarg);
      if (files == 0) {
        printf("Unset cache file limit\n");
      } else {
        printf("Set cache file limit to %u\n", files);
      }
      break;
    }

    case 'M': { // --max-size
      uint64_t size;
      if (!parse_size_with_suffix(optarg, &size)) {
        fatal("invalid size: %s", optarg);
      }
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_size", optarg);
      if (size == 0) {
        printf("Unset cache size limit\n");
      } else {
        char* s = format_human_readable_size(size);
        printf("Set cache size limit to %s\n", s);
        free(s);
      }
      break;
    }

    case 'o': {                          // --set-config
      char* p = strchr(optarg + 1, '='); // Improve error message for -o=K=V
      if (!p) {
        fatal("missing equal sign in \"%s\"", optarg);
      }
      char* key = x_strndup(optarg, p - optarg);
      char* value = p + 1;
      Config::set_value_in_file(ctx.config.primary_config_path(), key, value);
      free(key);
      break;
    }

    case 'p': // --show-config
      ctx.config.visit_items(configuration_printer);
      break;

    case 's': // --show-stats
      stats_summary(ctx.config);
      break;

    case 'V': // --version
      fprintf(stdout, VERSION_TEXT, CCACHE_VERSION);
      x_exit(0);

    case 'x': // --show-compression
    {
      ProgressBar progress_bar("Scanning...");
      compress_stats(ctx.config,
                     [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'X': // --recompress
    {
      int level;
      if (std::string(optarg) == "uncompressed") {
        level = 0;
      } else {
        level = Util::parse_int(optarg);
        if (level < -128 || level > 127) {
          throw Error("compression level must be between -128 and 127");
        }
        if (level == 0) {
          level = ctx.config.compression_level();
        }
      }

      ProgressBar progress_bar("Recompressing...");
      compress_recompress(
        ctx, level, [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'z': // --zero-stats
      stats_zero(ctx.config);
      printf("Statistics zeroed\n");
      break;

    default:
      fputs(USAGE_TEXT, stderr);
      x_exit(1);
    }

    // Some of the above switches might have changed config settings, so run the
    // setup again.
    set_up_config(ctx.config);
  }

  return 0;
}

int ccache_main(int argc, const char* const* argv);

int
ccache_main(int argc, const char* const* argv)
{
  try {
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
        return handle_main_options(argc, argv);
      }
    }

    return cache_compilation(argc, argv);
  } catch (const ErrorBase& e) {
    fmt::print(stderr, "ccache: error: {}\n", e.what());
    return EXIT_FAILURE;
  }
}
