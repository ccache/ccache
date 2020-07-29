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
#include "Fd.hpp"
#include "File.hpp"
#include "FormatNonstdStringView.hpp"
#include "Hash.hpp"
#include "MiniTrace.hpp"
#include "ProgressBar.hpp"
#include "Result.hpp"
#include "ResultDumper.hpp"
#include "ResultExtractor.hpp"
#include "ResultRetriever.hpp"
#include "SignalHandler.hpp"
#include "StdMakeUnique.hpp"
#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "argprocessing.hpp"
#include "cleanup.hpp"
#include "compopt.hpp"
#include "compress.hpp"
#include "exceptions.hpp"
#include "execute.hpp"
#include "hashutil.hpp"
#include "language.hpp"
#include "logging.hpp"
#include "manifest.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/string_view.hpp"

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#else
#  include "third_party/getopt_long.h"
#endif

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#include <algorithm>
#include <limits>

using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

static const char VERSION_TEXT[] =
  R"(%s version %s

Copyright (C) 2002-2007 Andrew Tridgell
Copyright (C) 2009-2020 Joel Rosdahl and other contributors

See <https://ccache.dev/credits.html> for a complete list of contributors.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.
)";

static const char USAGE_TEXT[] =
  R"(Usage:
    {} [options]
    {} compiler [compiler options]
    compiler [compiler options]          (via symbolic link)

Common options:
    -c, --cleanup              delete old files and recalculate size counters
                               (normally not needed as this is done
                               automatically)
    -C, --clear                clear the cache completely (except configuration)
        --evict-older-than AGE remove files older than AGE (unsigned integer
                               with a d (days) or s (seconds) suffix)
    -F, --max-files NUM        set maximum number of files in cache to NUM (use
                               0 for no limit)
    -M, --max-size SIZE        set maximum size of cache to SIZE (use 0 for no
                               limit); available suffixes: k, M, G, T (decimal)
                               and Ki, Mi, Gi, Ti (binary); default suffix: G
    -X, --recompress LEVEL     recompress the cache to LEVEL (integer level or
                               "uncompressed")
    -o, --set-config KEY=VAL   set configuration item KEY to value VAL
    -x, --show-compression     show compression statistics
    -p, --show-config          show current configuration options in
                               human-readable format
    -s, --show-stats           show summary of configuration and statistics
                               counters in human-readable format
    -z, --zero-stats           zero statistics counters

    -h, --help                 print this help text
    -V, --version              print version and copyright information

Options for scripting or debugging:
        --dump-manifest PATH   dump manifest file at PATH in text format
        --dump-result PATH     dump result file at PATH in text format
        --extract-result PATH  extract data stored in result file at PATH to the
                               current working directory
    -k, --get-config KEY       print the value of configuration key KEY
        --hash-file PATH       print the hash (160 bit BLAKE3) of the file at
                               PATH
        --print-stats          print statistics counter IDs and corresponding
                               values in machine-parsable format

See also <https://ccache.dev>.
)";

enum fromcache_call_mode { FROMCACHE_DIRECT_MODE, FROMCACHE_CPP_MODE };

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
static const int k_tempdir_cleanup_interval = 2 * 24 * 60 * 60; // 2 days

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
    std::string path = find_executable(ctx, word, MYNAME);
    if (path.empty()) {
      FATAL("{}: {}", word, strerror(errno));
    }

    prefix.push_back(path);
  }

  cc_log("Using command-line prefix %s", prefix_command.c_str());
  for (size_t i = prefix.size(); i != 0; i--) {
    args.push_front(prefix[i - 1]);
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

static void
clean_up_internal_tempdir(const Config& config)
{
  time_t now = time(nullptr);
  auto dir_st = Stat::stat(config.cache_dir(), Stat::OnError::log);
  if (!dir_st || dir_st.mtime() + k_tempdir_cleanup_interval >= now) {
    // No cleanup needed.
    return;
  }

  update_mtime(config.cache_dir().c_str());

  const std::string& temp_dir = config.temporary_dir();
  if (!Stat::lstat(temp_dir)) {
    return;
  }

  Util::traverse(temp_dir, [now](const std::string& path, bool is_dir) {
    if (is_dir) {
      return;
    }
    auto st = Stat::lstat(path, Stat::OnError::log);
    if (st && st.mtime() + k_tempdir_cleanup_interval < now) {
      Util::unlink_tmp(path);
    }
  });
}

static void
init_hash_debug(Context& ctx,
                Hash& hash,
                const char* obj_path,
                char type,
                const char* section_name,
                FILE* debug_text_file)
{
  if (!ctx.config.debug()) {
    return;
  }

  std::string path = fmt::format("{}.ccache-input-{}", obj_path, type);
  File debug_binary_file(path, "wb");
  if (debug_binary_file) {
    hash.enable_debug(section_name, debug_binary_file.get(), debug_text_file);
    ctx.hash_debug_files.push_back(std::move(debug_binary_file));
  } else {
    cc_log("Failed to open %s: %s", path.c_str(), strerror(errno));
  }
}

static GuessedCompiler
guess_compiler(const char* path)
{
  string_view name = Util::base_name(path);
  GuessedCompiler result = GuessedCompiler::unknown;
  if (name.find("clang") != std::string::npos) {
    result = GuessedCompiler::clang;
  } else if (name.find("gcc") != std::string::npos
             || name.find("g++") != std::string::npos) {
    result = GuessedCompiler::gcc;
  } else if (name.find("nvcc") != std::string::npos) {
    result = GuessedCompiler::nvcc;
  } else if (name == "pump" || name == "distcc-pump") {
    result = GuessedCompiler::pump;
  }
  return result;
}

static bool
do_remember_include_file(Context& ctx,
                         std::string path,
                         Hash& cpp_hash,
                         bool system,
                         Hash* depend_mode_hash)
{
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
  Hash fhash;

  is_pch = Util::is_precompiled_header(path);
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

    if (!hash_binary_file(ctx, fhash, path)) {
      return false;
    }
    cpp_hash.hash_delimiter(using_pch_sum ? "pch_sum_hash" : "pch_hash");
    cpp_hash.hash(fhash.digest().to_string());
  }

  if (ctx.config.direct_mode()) {
    if (!is_pch) { // else: the file has already been hashed.
      int result = hash_source_code_file(ctx, fhash, path);
      if (result & HASH_SOURCE_CODE_ERROR
          || result & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }
    }

    Digest d = fhash.digest();
    ctx.included_files.emplace(path, d);

    if (depend_mode_hash) {
      depend_mode_hash->hash_delimiter("include");
      depend_mode_hash->hash(d.to_string());
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
                      Hash& cpp_hash,
                      bool system,
                      Hash* depend_mode_hash)
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

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in the global variable
//   g_included_files.
static bool
process_preprocessed_file(Context& ctx, Hash& hash, const char* path, bool pump)
{
  std::string data;
  try {
    data = Util::read_file(path);
  } catch (Error&) {
    return false;
  }

  // Bytes between p and q are pending to be hashed.
  const char* p = data.data();
  char* q = const_cast<char*>(data.data()); // cast needed before C++17
  const char* end = data.c_str() + data.length();

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
        && (q == data.data() || q[-1] == '\n')) {
      // Workarounds for preprocessor linemarker bugs in GCC version 6.
      if (q[2] == '3') {
        if (str_startswith(q, "# 31 \"<command-line>\"\n")) {
          // Bogus extra line with #31, after the regular #1: Ignore the whole
          // line, and continue parsing.
          hash.hash(p, q - p);
          while (q < end && *q != '\n') {
            q++;
          }
          q++;
          p = q;
          continue;
        } else if (str_startswith(q, "# 32 \"<command-line>\" 2\n")) {
          // Bogus wrong line with #32, instead of regular #1: Replace the line
          // number with the usual one.
          hash.hash(p, q - p);
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
        return false;
      }
      // q points to the beginning of an include file path
      hash.hash(p, q - p);
      p = q;
      while (q < end && *q != '"') {
        q++;
      }
      // Look for preprocessor flags, after the "filename".
      bool system = false;
      const char* r = q + 1;
      while (r < end && *r != '\n') {
        if (*r == '3') { // System header.
          system = true;
        }
        r++;
      }
      // p and q span the include file path.
      std::string inc_path(p, q - p);
      if (!ctx.has_absolute_include_headers) {
        ctx.has_absolute_include_headers = Util::is_absolute_path(inc_path);
      }
      inc_path = Util::make_relative_path(ctx, inc_path);

      bool should_hash_inc_path = true;
      if (!ctx.config.hash_dir()) {
        if (Util::starts_with(inc_path, ctx.apparent_cwd)
            && Util::ends_with(inc_path, "//")) {
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
        hash.hash(inc_path);
      }

      remember_include_file(ctx, inc_path, hash, system, nullptr);
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

  hash.hash(p, (end - p));

  // Explicitly check the .gch/.pch/.pth file as Clang does not include any
  // mention of it in the preprocessed output.
  if (!ctx.included_pch_file.empty()) {
    std::string pch_path =
      Util::make_relative_path(ctx, ctx.included_pch_file.c_str());
    hash.hash(pch_path);
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
      adjusted_file_content.append(Util::make_relative_path(ctx, token));
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

  std::string tmp_file = output_dep + ".tmp";
  try {
    Util::write_file(tmp_file, adjusted_file_content);
  } catch (const Error& e) {
    cc_log(
      "Error writing temporary dependency file %s (%s), skip relative path"
      " usage",
      tmp_file.c_str(),
      e.what());
    Util::unlink_safe(tmp_file);
    return;
  }

  if (x_rename(tmp_file.c_str(), output_dep.c_str()) != 0) {
    cc_log(
      "Error renaming dependency file: %s -> %s (%s), skip relative path usage",
      tmp_file.c_str(),
      output_dep.c_str(),
      strerror(errno));
    Util::unlink_safe(tmp_file);
  } else {
    cc_log("Renamed dependency file: %s -> %s",
           tmp_file.c_str(),
           output_dep.c_str());
  }
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static optional<Digest>
result_name_from_depfile(Context& ctx, Hash& hash)
{
  std::string file_content;
  try {
    file_content = Util::read_file(ctx.args_info.output_dep);
  } catch (const Error& e) {
    cc_log("Cannot open dependency file %s: %s",
           ctx.args_info.output_dep.c_str(),
           e.what());
    return nullopt;
  }

  for (string_view token : Util::split_into_views(file_content, " \t\r\n")) {
    if (token == "\\" || token.ends_with(":")) {
      continue;
    }
    if (!ctx.has_absolute_include_headers) {
      ctx.has_absolute_include_headers = Util::is_absolute_path(token);
    }
    std::string path = Util::make_relative_path(ctx, token);
    remember_include_file(ctx, path, hash, false, &hash);
  }

  // Explicitly check the .gch/.pch/.pth file as it may not be mentioned in the
  // dependencies output.
  if (!ctx.included_pch_file.empty()) {
    std::string pch_path =
      Util::make_relative_path(ctx, ctx.included_pch_file.c_str());
    hash.hash(pch_path);
    remember_include_file(ctx, pch_path, hash, false, nullptr);
  }

  bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
  if (debug_included) {
    print_included_files(ctx, stdout);
  }

  return hash.digest();
}

// Execute the compiler/preprocessor, with logic to retry without requesting
// colored diagnostics messages if that fails.
static int
do_execute(Context& ctx,
           Args& args,
           TemporaryFile&& tmp_stdout,
           TemporaryFile&& tmp_stderr)
{
  if (ctx.diagnostics_color_failed
      && ctx.guessed_compiler == GuessedCompiler::gcc) {
    args.erase_with_prefix("-fdiagnostics-color");
  }
  int status = execute(args.to_argv().data(),
                       std::move(tmp_stdout.fd),
                       std::move(tmp_stderr.fd),
                       &ctx.compiler_pid);
  if (status != 0 && !ctx.diagnostics_color_failed
      && ctx.guessed_compiler == GuessedCompiler::gcc) {
    auto errors = Util::read_file(tmp_stderr.path);
    if (errors.find("unrecognized command line option") != std::string::npos
        && errors.find("-fdiagnostics-color") != std::string::npos) {
      // Old versions of GCC do not support colored diagnostics.
      cc_log("-fdiagnostics-color is unsupported; trying again without it");

      tmp_stdout.fd = Fd(open(
        tmp_stdout.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stdout.fd) {
        cc_log("Failed to truncate %s: %s",
               tmp_stdout.path.c_str(),
               strerror(errno));
        failed(STATS_ERROR);
      }

      tmp_stderr.fd = Fd(open(
        tmp_stderr.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stderr.fd) {
        cc_log("Failed to truncate %s: %s",
               tmp_stderr.path.c_str(),
               strerror(errno));
        failed(STATS_ERROR);
      }

      ctx.diagnostics_color_failed = true;
      return do_execute(
        ctx, args, std::move(tmp_stdout), std::move(tmp_stderr));
    }
  }
  return status;
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
         Hash* depend_mode_hash)
{
  args.push_back("-o");
  args.push_back(ctx.args_info.output_obj);

  if (ctx.config.hard_link() && ctx.args_info.output_obj != "/dev/null") {
    // Workaround for Clang bug where it overwrites an existing object file
    // when it's compiling an assembler file, see
    // <https://bugs.llvm.org/show_bug.cgi?id=39782>.
    Util::unlink_safe(ctx.args_info.output_obj);
  }

  if (ctx.args_info.generating_diagnostics) {
    args.push_back("--serialize-diagnostics");
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
    if (unlink(ctx.args_info.output_dwo.c_str()) != 0 && errno != ENOENT
        && errno != ESTALE) {
      cc_log("Failed to unlink %s: %s",
             ctx.args_info.output_dwo.c_str(),
             strerror(errno));
      failed(STATS_BADOUTPUTFILE);
    }
  }

  cc_log("Running real compiler");
  MTR_BEGIN("execute", "compiler");

  TemporaryFile tmp_stdout(
    fmt::format("{}/tmp.stdout", ctx.config.temporary_dir()));
  ctx.register_pending_tmp_file(tmp_stdout.path);
  std::string tmp_stdout_path = tmp_stdout.path;

  TemporaryFile tmp_stderr(
    fmt::format("{}/tmp.stderr", ctx.config.temporary_dir()));
  ctx.register_pending_tmp_file(tmp_stderr.path);
  std::string tmp_stderr_path = tmp_stderr.path;

  int status;
  if (!ctx.config.depend_mode()) {
    status =
      do_execute(ctx, args, std::move(tmp_stdout), std::move(tmp_stderr));
    args.pop_back(3);
  } else {
    // Use the original arguments (including dependency options) in depend
    // mode.
    Args depend_mode_args = ctx.orig_args;
    depend_mode_args.erase_with_prefix("--ccache-");
    depend_mode_args.push_back(depend_extra_args);
    add_prefix(ctx, depend_mode_args, ctx.config.prefix_command());

    ctx.time_of_compilation = time(nullptr);
    status = do_execute(
      ctx, depend_mode_args, std::move(tmp_stdout), std::move(tmp_stderr));
  }
  MTR_END("execute", "compiler");

  auto st = Stat::stat(tmp_stdout_path, Stat::OnError::log);
  if (!st) {
    // The stdout file was removed - cleanup in progress? Better bail out.
    failed(STATS_MISSING);
  }

  // distcc-pump outputs lines like this:
  // __________Using # distcc servers in pump mode
  if (st.size() != 0 && ctx.guessed_compiler != GuessedCompiler::pump) {
    cc_log("Compiler produced stdout");
    failed(STATS_STDOUT);
  }

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler into tmp_stderr.
  if (!ctx.cpp_stderr.empty()) {
    std::string combined_stderr =
      Util::read_file(ctx.cpp_stderr) + Util::read_file(tmp_stderr_path);
    Util::write_file(tmp_stderr_path, combined_stderr);
  }

  if (status != 0) {
    cc_log("Compiler gave exit status %d", status);

    // We can output stderr immediately instead of rerunning the compiler.
    Util::send_to_stderr(Util::read_file(tmp_stderr_path),
                         ctx.args_info.strip_diagnostics_colors);

    failed(STATS_STATUS, status);
  }

  if (ctx.config.depend_mode()) {
    assert(depend_mode_hash);
    auto result_name = result_name_from_depfile(ctx, *depend_mode_hash);
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

  st = Stat::stat(tmp_stderr_path, Stat::OnError::log);
  if (!st) {
    failed(STATS_ERROR);
  }

  auto orig_dest_stat = Stat::stat(ctx.result_path());
  Result::Writer result_writer(ctx, ctx.result_path());

  if (st.size() > 0) {
    result_writer.write(Result::FileType::stderr_output, tmp_stderr_path);
  }
  result_writer.write(Result::FileType::object, ctx.args_info.output_obj);
  if (ctx.args_info.generating_dependencies) {
    result_writer.write(Result::FileType::dependency, ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    result_writer.write(Result::FileType::coverage, ctx.args_info.output_cov);
  }
  if (ctx.args_info.generating_stackusage) {
    result_writer.write(Result::FileType::stackusage, ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    result_writer.write(Result::FileType::diagnostic, ctx.args_info.output_dia);
  }
  if (ctx.args_info.seen_split_dwarf && Stat::stat(ctx.args_info.output_dwo)) {
    // Only store .dwo file if it was created by the compiler (GCC and Clang
    // behave differently e.g. for "-gsplit-dwarf -g1").
    result_writer.write(Result::FileType::dwarf_object,
                        ctx.args_info.output_dwo);
  }

  auto error = result_writer.finalize();
  if (error) {
    cc_log("Error: %s", error->c_str());
  } else {
    cc_log("Stored in cache: %s", ctx.result_path().c_str());
  }

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
  std::string first_level_dir(Util::dir_name(ctx.stats_file()));
  if (!create_cachedir_tag(first_level_dir)) {
    cc_log("Failed to create %s/CACHEDIR.TAG (%s)",
           first_level_dir.c_str(),
           strerror(errno));
  }

  // Everything OK.
  Util::send_to_stderr(Util::read_file(tmp_stderr_path),
                       ctx.args_info.strip_diagnostics_colors);
}

// Find the result name by running the compiler in preprocessor mode and
// hashing the result.
static Digest
get_result_name_from_cpp(Context& ctx, Args& args, Hash& hash)
{
  ctx.time_of_compilation = time(nullptr);

  std::string stderr_path;
  std::string stdout_path;
  int status;
  if (ctx.args_info.direct_i_file) {
    // We are compiling a .i or .ii file - that means we can skip the cpp stage
    // and directly form the correct i_tmpfile.
    stdout_path = ctx.args_info.input_file;
    status = 0;
  } else {
    // Run cpp on the input file to obtain the .i.

    TemporaryFile tmp_stdout(
      fmt::format("{}/tmp.cpp_stdout", ctx.config.temporary_dir()));
    stdout_path = tmp_stdout.path;
    ctx.register_pending_tmp_file(stdout_path);

    TemporaryFile tmp_stderr(
      fmt::format("{}/tmp.cpp_stderr", ctx.config.temporary_dir()));
    stderr_path = tmp_stderr.path;
    ctx.register_pending_tmp_file(stderr_path);

    size_t args_added = 2;
    args.push_back("-E");
    if (ctx.config.keep_comments_cpp()) {
      args.push_back("-C");
      args_added = 3;
    }
    args.push_back(ctx.args_info.input_file);
    add_prefix(ctx, args, ctx.config.prefix_command_cpp());
    cc_log("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    status =
      do_execute(ctx, args, std::move(tmp_stdout), std::move(tmp_stderr));
    MTR_END("execute", "preprocessor");
    args.pop_back(args_added);
  }

  if (status != 0) {
    cc_log("Preprocessor gave exit status %d", status);
    failed(STATS_PREPROCESSOR);
  }

  hash.hash_delimiter("cpp");
  bool is_pump = ctx.guessed_compiler == GuessedCompiler::pump;
  if (!process_preprocessed_file(ctx, hash, stdout_path.c_str(), is_pump)) {
    failed(STATS_ERROR);
  }

  hash.hash_delimiter("cppstderr");
  if (!ctx.args_info.direct_i_file
      && !hash_binary_file(ctx, hash, stderr_path)) {
    // Somebody removed the temporary file?
    cc_log("Failed to open %s: %s", stderr_path.c_str(), strerror(errno));
    failed(STATS_ERROR);
  }

  if (ctx.args_info.direct_i_file) {
    ctx.i_tmpfile = ctx.args_info.input_file;
  } else {
    // i_tmpfile needs the proper cpp_extension for the compiler to do its
    // thing correctly
    ctx.i_tmpfile =
      fmt::format("{}.{}", stdout_path, ctx.config.cpp_extension());
    x_rename(stdout_path.c_str(), ctx.i_tmpfile.c_str());
    ctx.register_pending_tmp_file(ctx.i_tmpfile);
  }

  if (!ctx.config.run_second_cpp()) {
    // If we are using the CPP trick, we need to remember this stderr data and
    // output it just before the main stderr from the compiler pass.
    ctx.cpp_stderr = stderr_path;
    hash.hash_delimiter("runsecondcpp");
    hash.hash("false");
  }

  return hash.digest();
}

// Hash mtime or content of a file, or the output of a command, according to
// the CCACHE_COMPILERCHECK setting.
static void
hash_compiler(const Context& ctx,
              Hash& hash,
              const Stat& st,
              const char* path,
              bool allow_command)
{
  if (ctx.config.compiler_check() == "none") {
    // Do nothing.
  } else if (ctx.config.compiler_check() == "mtime") {
    hash.hash_delimiter("cc_mtime");
    hash.hash(st.size());
    hash.hash(st.mtime());
  } else if (Util::starts_with(ctx.config.compiler_check(), "string:")) {
    hash.hash_delimiter("cc_hash");
    hash.hash(ctx.config.compiler_check().c_str() + strlen("string:"));
  } else if (ctx.config.compiler_check() == "content" || !allow_command) {
    hash.hash_delimiter("cc_content");
    hash_binary_file(ctx, hash, path);
  } else { // command string
    if (!hash_multicommand_output(
          hash, ctx.config.compiler_check(), ctx.orig_args[0])) {
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
                        Hash& hash,
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
        std::string path = fmt::format("{}/{}", ccbin, compiler);
        auto st = Stat::stat(path);
        if (st) {
          hash_compiler(ctx, hash, st, path.c_str(), false);
        }
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

static bool
should_rewrite_dependency_target(const ArgsInfo& args_info)
{
  return !args_info.dependency_target_specified && args_info.seen_MD_MMD;
}

// update a hash with information common for the direct and preprocessor modes.
static void
hash_common_info(const Context& ctx,
                 const Args& args,
                 Hash& hash,
                 const ArgsInfo& args_info)
{
  hash.hash(HASH_PREFIX);

  // We have to hash the extension, as a .i file isn't treated the same by the
  // compiler as a .ii file.
  hash.hash_delimiter("ext");
  hash.hash(ctx.config.cpp_extension().c_str());

#ifdef _WIN32
  const std::string compiler_path = Win32Util::add_exe_suffix(args[0]);
#else
  const std::string compiler_path = args[0];
#endif

  auto st = Stat::stat(compiler_path, Stat::OnError::log);
  if (!st) {
    failed(STATS_COMPILER);
  }

  // Hash information about the compiler.
  hash_compiler(ctx, hash, st, compiler_path.c_str(), true);

  // Also hash the compiler name as some compilers use hard links and behave
  // differently depending on the real name.
  hash.hash_delimiter("cc_name");
  hash.hash(Util::base_name(args[0]));

  if (!(ctx.config.sloppiness() & SLOPPY_LOCALE)) {
    // Hash environment variables that may affect localization of compiler
    // warning messages.
    const char* envvars[] = {
      "LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", nullptr};
    for (const char** p = envvars; *p; ++p) {
      char* v = getenv(*p);
      if (v) {
        hash.hash_delimiter(*p);
        hash.hash(v);
      }
    }
  }

  // Possibly hash the current working directory.
  if (args_info.generating_debuginfo && ctx.config.hash_dir()) {
    std::string dir_to_hash = ctx.apparent_cwd;
    for (const auto& map : args_info.debug_prefix_maps) {
      size_t sep_pos = map.find('=');
      if (sep_pos != std::string::npos) {
        std::string old_path = map.substr(0, sep_pos);
        std::string new_path = map.substr(sep_pos + 1);
        cc_log("Relocating debuginfo from %s to %s (CWD: %s)",
               old_path.c_str(),
               new_path.c_str(),
               ctx.apparent_cwd.c_str());
        if (Util::starts_with(ctx.apparent_cwd, old_path)) {
          dir_to_hash = new_path + ctx.apparent_cwd.substr(old_path.size());
        }
      }
    }
    cc_log("Hashing CWD %s", dir_to_hash.c_str());
    hash.hash_delimiter("cwd");
    hash.hash(dir_to_hash);
  }

  if ((!should_rewrite_dependency_target(ctx.args_info)
       && ctx.args_info.generating_dependencies)
      || ctx.args_info.seen_split_dwarf) {
    // The output object file name is part of the .d file, so include the path
    // in the hash if generating dependencies.
    //
    // Object files include a link to the corresponding .dwo file based on the
    // target object filename when using -gsplit-dwarf, so hashing the object
    // file path will do it, although just hashing the object file base name
    // would be enough.
    hash.hash_delimiter("object file");
    hash.hash(ctx.args_info.output_obj);
  }

  // Possibly hash the coverage data file path.
  if (ctx.args_info.generating_coverage && ctx.args_info.profile_arcs) {
    std::string dir;
    if (!ctx.args_info.profile_path.empty()) {
      dir = ctx.args_info.profile_path;
    } else {
      dir =
        Util::real_path(std::string(Util::dir_name(ctx.args_info.output_obj)));
    }
    string_view stem =
      Util::remove_extension(Util::base_name(ctx.args_info.output_obj));
    std::string gcda_path = fmt::format("{}/{}.gcda", dir, stem);
    cc_log("Hashing coverage path %s", gcda_path.c_str());
    hash.hash_delimiter("gcda");
    hash.hash(gcda_path);
  }

  // Possibly hash the sanitize blacklist file path.
  for (const auto& sanitize_blacklist : args_info.sanitize_blacklists) {
    cc_log("Hashing sanitize blacklist %s", sanitize_blacklist.c_str());
    hash.hash("sanitizeblacklist");
    if (!hash_binary_file(ctx, hash, sanitize_blacklist)) {
      failed(STATS_BADEXTRAFILE);
    }
  }

  if (!ctx.config.extra_files_to_hash().empty()) {
    for (const std::string& path : Util::split_into_strings(
           ctx.config.extra_files_to_hash(), PATH_DELIM)) {
      cc_log("Hashing extra file %s", path.c_str());
      hash.hash_delimiter("extrafile");
      if (!hash_binary_file(ctx, hash, path)) {
        failed(STATS_BADEXTRAFILE);
      }
    }
  }

  // Possibly hash GCC_COLORS (for color diagnostics).
  if (ctx.guessed_compiler == GuessedCompiler::gcc) {
    const char* gcc_colors = getenv("GCC_COLORS");
    if (gcc_colors) {
      hash.hash_delimiter("gcccolors");
      hash.hash(gcc_colors);
    }
  }
}

static bool
hash_profile_data_file(const Context& ctx, Hash& hash)
{
  const std::string& profile_path = ctx.args_info.profile_path;
  string_view base_name = Util::remove_extension(ctx.args_info.output_obj);
  std::string hashified_cwd = ctx.apparent_cwd;
  std::replace(hashified_cwd.begin(), hashified_cwd.end(), '/', '#');

  std::vector<std::string> paths_to_try{
    // -fprofile-use[=dir]/-fbranch-probabilities (GCC <9)
    fmt::format("{}/{}.gcda", profile_path, base_name),
    // -fprofile-use[=dir]/-fbranch-probabilities (GCC >=9)
    fmt::format("{}/{}#{}.gcda", profile_path, hashified_cwd, base_name),
    // -fprofile(-instr|-sample)-use=file (Clang), -fauto-profile=file (GCC >=5)
    profile_path,
    // -fprofile(-instr|-sample)-use=dir (Clang)
    fmt::format("{}/default.profdata", profile_path),
    // -fauto-profile (GCC >=5)
    "fbdata.afdo", // -fprofile-dir is not used
  };

  bool found = false;
  for (const std::string& p : paths_to_try) {
    cc_log("Checking for profile data file %s", p.c_str());
    auto st = Stat::stat(p);
    if (st && !st.is_directory()) {
      cc_log("Adding profile data %s to the hash", p.c_str());
      hash.hash_delimiter("-fprofile-use");
      if (hash_binary_file(ctx, hash, p)) {
        found = true;
      }
    }
  }

  return found;
}

static bool
option_should_be_ignored(const std::string& arg,
                         const std::vector<std::string>& patterns)
{
  return std::any_of(
    patterns.cbegin(), patterns.cend(), [&arg](const std::string& pattern) {
      const auto& prefix = string_view(pattern).substr(0, pattern.length() - 1);
      return (
        pattern == arg
        || (Util::ends_with(pattern, "*") && Util::starts_with(arg, prefix)));
    });
}

// Update a hash sum with information specific to the direct and preprocessor
// modes and calculate the result name. Returns the result name on success,
// otherwise nullopt.
static optional<Digest>
calculate_result_name(Context& ctx,
                      const Args& args,
                      Args& preprocessor_args,
                      Hash& hash,
                      bool direct_mode)
{
  bool found_ccbin = false;

  hash.hash_delimiter("result version");
  hash.hash(Result::k_version);

  if (direct_mode) {
    hash.hash_delimiter("manifest version");
    hash.hash(k_manifest_version);
  }

  // clang will emit warnings for unused linker flags, so we shouldn't skip
  // those arguments.
  int is_clang = ctx.guessed_compiler == GuessedCompiler::clang
                 || ctx.guessed_compiler == GuessedCompiler::unknown;

  // First the arguments.
  for (size_t i = 1; i < args.size(); i++) {
    // Trust the user if they've said we should not hash a given option.
    if (option_should_be_ignored(args[i], ctx.ignore_options())) {
      cc_log("Not hashing ignored option: %s", args[i].c_str());
      if (i + 1 < args.size() && compopt_takes_arg(args[i])) {
        i++;
        cc_log("Not hashing argument of ignored option: %s", args[i].c_str());
      }
      continue;
    }

    // -L doesn't affect compilation (except for clang).
    if (i < args.size() - 1 && args[i] == "-L" && !is_clang) {
      i++;
      continue;
    }
    if (Util::starts_with(args[i], "-L") && !is_clang) {
      continue;
    }

    // -Wl,... doesn't affect compilation (except for clang).
    if (Util::starts_with(args[i], "-Wl,") && !is_clang) {
      continue;
    }

    // The -fdebug-prefix-map option may be used in combination with
    // CCACHE_BASEDIR to reuse results across different directories. Skip using
    // the value of the option from hashing but still hash the existence of the
    // option.
    if (Util::starts_with(args[i], "-fdebug-prefix-map=")) {
      hash.hash_delimiter("arg");
      hash.hash("-fdebug-prefix-map=");
      continue;
    }
    if (Util::starts_with(args[i], "-ffile-prefix-map=")) {
      hash.hash_delimiter("arg");
      hash.hash("-ffile-prefix-map=");
      continue;
    }
    if (Util::starts_with(args[i], "-fmacro-prefix-map=")) {
      hash.hash_delimiter("arg");
      hash.hash("-fmacro-prefix-map=");
      continue;
    }

    // When using the preprocessor, some arguments don't contribute to the
    // hash. The theory is that these arguments will change the output of -E if
    // they are going to have any effect at all. For precompiled headers this
    // might not be the case.
    if (!direct_mode && !ctx.args_info.output_is_precompiled_header
        && !ctx.args_info.using_precompiled_header) {
      if (compopt_affects_cpp(args[i])) {
        if (compopt_takes_arg(args[i])) {
          i++;
        }
        continue;
      }
      if (compopt_short(compopt_affects_cpp, args[i])) {
        continue;
      }
    }

    // If we're generating dependencies, we make sure to skip the filename of
    // the dependency file, since it doesn't impact the output.
    if (ctx.args_info.generating_dependencies) {
      if (Util::starts_with(args[i], "-Wp,")) {
        if (Util::starts_with(args[i], "-Wp,-MD,")
            && !strchr(args[i].c_str() + 8, ',')) {
          hash.hash(args[i].c_str(), 8);
          continue;
        } else if (Util::starts_with(args[i], "-Wp,-MMD,")
                   && !strchr(args[i].c_str() + 9, ',')) {
          hash.hash(args[i].c_str(), 9);
          continue;
        }
      } else if (Util::starts_with(args[i], "-MF")) {
        // In either case, hash the "-MF" part.
        hash.hash_delimiter("arg");
        hash.hash(args[i].c_str(), 3);

        if (ctx.args_info.output_dep != "/dev/null") {
          bool separate_argument = (args[i].size() == 3);
          if (separate_argument) {
            // Next argument is dependency name, so skip it.
            i++;
          }
        }
        continue;
      }
    }

    const char* p = nullptr;
    if (Util::starts_with(args[i], "-specs=")) {
      p = args[i].c_str() + 7;
    } else if (Util::starts_with(args[i], "--specs=")) {
      p = args[i].c_str() + 8;
    }

    if (p) {
      auto st = Stat::stat(p, Stat::OnError::log);
      if (st) {
        // If given an explicit specs file, then hash that file, but don't
        // include the path to it in the hash.
        hash.hash_delimiter("specs");
        hash_compiler(ctx, hash, st, p, false);
        continue;
      }
    }

    if (Util::starts_with(args[i], "-fplugin=")) {
      auto st = Stat::stat(args[i].c_str() + 9, Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        hash_compiler(ctx, hash, st, args[i].c_str() + 9, false);
        continue;
      }
    }

    if (args[i] == "-Xclang" && i + 3 < args.size() && args[i + 1] == "-load"
        && args[i + 2] == "-Xclang") {
      auto st = Stat::stat(args[i + 3], Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        hash_compiler(ctx, hash, st, args[i + 3].c_str(), false);
        i += 3;
        continue;
      }
    }

    if ((args[i] == "-ccbin" || args[i] == "--compiler-bindir")
        && i + 1 < args.size()) {
      auto st = Stat::stat(args[i + 1]);
      if (st) {
        found_ccbin = true;
        hash.hash_delimiter("ccbin");
        hash_nvcc_host_compiler(ctx, hash, &st, args[i + 1].c_str());
        i++;
        continue;
      }
    }

    // All other arguments are included in the hash.
    hash.hash_delimiter("arg");
    hash.hash(args[i]);
    if (i + 1 < args.size() && compopt_takes_arg(args[i])) {
      i++;
      hash.hash_delimiter("arg");
      hash.hash(args[i]);
    }
  }

  // Make results with dependency file /dev/null different from those without
  // it.
  if (ctx.args_info.generating_dependencies
      && ctx.args_info.output_dep == "/dev/null") {
    hash.hash_delimiter("/dev/null dependency file");
  }

  if (!found_ccbin && ctx.args_info.actual_language == "cu") {
    hash_nvcc_host_compiler(ctx, hash, nullptr, nullptr);
  }

  // For profile generation (-fprofile(-instr)-generate[=path])
  // - hash profile path
  //
  // For profile usage (-fprofile(-instr|-sample)-use, -fbranch-probabilities):
  // - hash profile data
  //
  // -fbranch-probabilities and -fvpt usage is covered by
  // -fprofile-generate/-fprofile-use.
  //
  // The profile directory can be specified as an argument to
  // -fprofile(-instr)-generate=, -fprofile(-instr|-sample)-use= or
  // -fprofile-dir=.

  if (ctx.args_info.profile_generate) {
    assert(!ctx.args_info.profile_path.empty());
    cc_log("Adding profile directory %s to our hash",
           ctx.args_info.profile_path.c_str());
    hash.hash_delimiter("-fprofile-dir");
    hash.hash(ctx.args_info.profile_path);
  }

  if (ctx.args_info.profile_use && !hash_profile_data_file(ctx, hash)) {
    cc_log("No profile data file found");
    failed(STATS_NOINPUT);
  }

  // Adding -arch to hash since cpp output is affected.
  for (const auto& arch : ctx.args_info.arch_args) {
    hash.hash_delimiter("-arch");
    hash.hash(arch);
  }

  optional<Digest> result_name;
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
        hash.hash_delimiter(*p);
        hash.hash(v);
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
    hash.hash_delimiter("inputfile");
    hash.hash(ctx.args_info.input_file);

    hash.hash_delimiter("sourcecode");
    int result = hash_source_code_file(ctx, hash, ctx.args_info.input_file);
    if (result & HASH_SOURCE_CODE_ERROR) {
      failed(STATS_ERROR);
    }
    if (result & HASH_SOURCE_CODE_FOUND_TIME) {
      cc_log("Disabling direct mode");
      ctx.config.set_direct_mode(false);
      return nullopt;
    }

    ctx.set_manifest_name(hash.digest());

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
    if (ctx.args_info.arch_args.empty()) {
      result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
      cc_log("Got result name from preprocessor");
    } else {
      preprocessor_args.push_back("-arch");
      for (size_t i = 0; i < ctx.args_info.arch_args.size(); ++i) {
        preprocessor_args.push_back(ctx.args_info.arch_args[i]);
        result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
        cc_log("Got result name from preprocessor with -arch %s",
               ctx.args_info.arch_args[i].c_str());
        if (i != ctx.args_info.arch_args.size() - 1) {
          result_name = nullopt;
        }
        preprocessor_args.pop_back();
      }
      preprocessor_args.pop_back();
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
      && !ctx.args_info.fno_pch_timestamp && mode == FROMCACHE_CPP_MODE) {
    cc_log("Not considering cached precompiled header in preprocessor mode");
    return nullopt;
  }

  MTR_BEGIN("cache", "from_cache");

  // Get result from cache.
  Result::Reader result_reader(ctx.result_path());
  ResultRetriever result_retriever(
    ctx, should_rewrite_dependency_target(ctx.args_info));

  auto error = result_reader.read(result_retriever);
  if (error) {
    cc_log("Failed to get result from cache: %s", error->c_str());
    return nullopt;
  } else {
    // Update modification timestamp to save file from LRU cleanup.
    update_mtime(ctx.result_path().c_str());
  }

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
  if (Util::same_program_name(base, MYNAME)) {
    ctx.orig_args.pop_front();
    if (is_full_path(ctx.orig_args[0].c_str())) {
      // A full path was given.
      return;
    }
    base = std::string(Util::base_name(ctx.orig_args[0]));
  }

  // Support user override of the compiler.
  if (!ctx.config.compiler().empty()) {
    base = ctx.config.compiler();
  }

  std::string compiler = find_executable(ctx, base, MYNAME);
  if (compiler.empty()) {
    FATAL("Could not find compiler \"{}\" in PATH", base);
  }
  if (compiler == argv[0]) {
    FATAL("Recursive invocation (the name of the ccache binary must be \"{}\")",
          MYNAME);
  }
  ctx.orig_args[0] = compiler;
}

static void
create_initial_config_file(Config& config)
{
  if (!Util::create_dir(Util::dir_name(config.primary_config_path()))) {
    return;
  }

  unsigned max_files;
  uint64_t max_size;
  std::string stats_dir = fmt::format("{}/0", config.cache_dir());
  if (Stat::stat(stats_dir)) {
    stats_get_obsolete_limits(stats_dir, &max_files, &max_size);
    // STATS_MAXFILES and STATS_MAXSIZE was stored for each top directory.
    max_files *= 16;
    max_size *= 16;
  } else {
    max_files = 0;
    max_size = config.max_size();
  }

  FILE* f = fopen(config.primary_config_path().c_str(), "w");
  if (!f) {
    return;
  }
  if (max_files != 0) {
    fprintf(f, "max_files = %u\n", max_files);
    config.set_max_files(max_files);
  }
  if (max_size != 0) {
    std::string size = Util::format_parsable_size_with_suffix(max_size);
    fprintf(f, "max_size = %s\n", size.c_str());
    config.set_max_size(max_size);
  }
  fclose(f);
}

// Read config file(s), populate variables, create configuration file in cache
// directory if missing, etc.
static void
set_up_config(Config& config)
{
  char* p = getenv("CCACHE_CONFIGPATH");
  if (p) {
    config.set_primary_config_path(p);
  } else {
    config.set_secondary_config_path(fmt::format("{}/ccache.conf", SYSCONFDIR));
    MTR_BEGIN("config", "conf_read_secondary");
    // A missing config file in SYSCONFDIR is OK so don't check return value.
    config.update_from_file(config.secondary_config_path());
    MTR_END("config", "conf_read_secondary");

    if (config.cache_dir().empty()) {
      FATAL("configuration setting \"cache_dir\" must not be the empty string");
    }
    if ((p = getenv("CCACHE_DIR"))) {
      config.set_cache_dir(p);
    }
    if (config.cache_dir().empty()) {
      FATAL("CCACHE_DIR must not be the empty string");
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
  ctx.orig_args = Args::from_argv(argc, argv);
  ctx.ignore_header_paths = Util::split_into_strings(
    ctx.config.ignore_headers_in_manifest(), PATH_DELIM);
  ctx.set_ignore_options(
    Util::split_into_strings(ctx.config.ignore_options(), " "));
}

// Initialize ccache, must be called once before anything else is run.
static void
initialize(Context& ctx, int argc, const char* const* argv)
{
  set_up_config(ctx.config);
  set_up_context(ctx, argc, argv);
  init_log(ctx.config);

  cc_log("=== CCACHE %s STARTED =========================================",
         CCACHE_VERSION);

  if (getenv("CCACHE_INTERNAL_TRACE")) {
#ifdef MTR_ENABLED
    ctx.mini_trace = std::make_unique<MiniTrace>(ctx.args_info);
#else
    cc_log("Error: tracing is not enabled!");
#endif
  }
}

// Make a copy of stderr that will not be cached, so things like distcc can
// send networking errors to it.
static void
set_up_uncached_err()
{
  int uncached_fd =
    dup(STDERR_FILENO); // The file descriptor is intentionally leaked.
  if (uncached_fd == -1) {
    cc_log("dup(2) failed: %s", strerror(errno));
    failed(STATS_ERROR);
  }

  x_setenv("UNCACHED_ERR_FD", fmt::format("{}", uncached_fd).c_str());
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
  // Needed for portability when using localtime_r.
  tzset();

  auto ctx = std::make_unique<Context>();
  SignalHandler signal_handler(*ctx);

  initialize(*ctx, argc, argv);

  MTR_BEGIN("main", "find_compiler");
  find_compiler(*ctx, argv);
  MTR_END("main", "find_compiler");

  try {
    enum stats stat = do_cache_compilation(*ctx, argv);
    stats_update(*ctx, stat);
    return EXIT_SUCCESS;
  } catch (const Failure& e) {
    if (e.stat() != STATS_NONE) {
      stats_update(*ctx, e.stat());
    }

    if (e.exit_code()) {
      return *e.exit_code();
    }
    // Else: Fall back to running the real compiler.

    assert(!ctx->orig_args.empty());

    ctx->orig_args.erase_with_prefix("--ccache-");
    add_prefix(*ctx, ctx->orig_args, ctx->config.prefix_command());

    cc_log("Failed; falling back to running the real compiler");

    Args saved_orig_args(std::move(ctx->orig_args));
    auto execv_argv = saved_orig_args.to_argv();

    cc_log_argv("Executing ", execv_argv.data());
    ctx.reset(); // Dump debug logs last thing before executing.
    execv(execv_argv[0], const_cast<char* const*>(execv_argv.data()));
    FATAL("execv of {} failed: {}", execv_argv[0], strerror(errno));
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
  if (ctx.config.temporary_dir() == ctx.config.cache_dir() + "/tmp") {
    clean_up_internal_tempdir(ctx.config);
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
  ctx.guessed_compiler = guess_compiler(ctx.orig_args[0].c_str());
  MTR_END("main", "guess_compiler");

  // Arguments (except -E) to send to the preprocessor.
  Args preprocessor_args;
  // Arguments not sent to the preprocessor but that should be part of the
  // hash.
  Args extra_args_to_hash;
  // Arguments to send to the real compiler.
  Args compiler_args;
  MTR_BEGIN("main", "process_args");

  auto error =
    process_args(ctx, preprocessor_args, extra_args_to_hash, compiler_args);
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
  MTR_META_THREAD_NAME(ctx.args_info.output_obj.c_str());

  if (ctx.config.debug()) {
    std::string path =
      fmt::format("{}.ccache-input-text", ctx.args_info.output_obj);
    File debug_text_file(path, "w");
    if (debug_text_file) {
      ctx.hash_debug_files.push_back(std::move(debug_text_file));
    } else {
      cc_log("Failed to open %s: %s", path.c_str(), strerror(errno));
    }
  }

  FILE* debug_text_file = !ctx.hash_debug_files.empty()
                            ? ctx.hash_debug_files.front().get()
                            : nullptr;

  Hash common_hash;
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
  Hash direct_hash = common_hash;
  init_hash_debug(ctx,
                  direct_hash,
                  ctx.args_info.output_obj.c_str(),
                  'd',
                  "DIRECT MODE",
                  debug_text_file);

  Args args_to_hash = preprocessor_args;
  args_to_hash.push_back(extra_args_to_hash);

  bool put_result_in_manifest = false;
  optional<Digest> result_name;
  optional<Digest> result_name_from_manifest;
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
    Hash cpp_hash = common_hash;
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

    // calculate_result_name does not return nullopt if the last (direct_mode)
    // argument is false.
    assert(result_name);
    ctx.set_result_name(*result_name);

    if (result_name_from_manifest && result_name_from_manifest != result_name) {
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
      Util::unlink_safe(ctx.manifest_path());

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
  Hash* depend_mode_hash = ctx.config.depend_mode() ? &direct_hash : nullptr;

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
    EVICT_OLDER_THAN,
    EXTRACT_RESULT,
    HASH_FILE,
    PRINT_STATS,
  };
  static const struct option options[] = {
    {"cleanup", no_argument, nullptr, 'c'},
    {"clear", no_argument, nullptr, 'C'},
    {"dump-manifest", required_argument, nullptr, DUMP_MANIFEST},
    {"dump-result", required_argument, nullptr, DUMP_RESULT},
    {"evict-older-than", required_argument, nullptr, EVICT_OLDER_THAN},
    {"extract-result", required_argument, nullptr, EXTRACT_RESULT},
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

  Context ctx;
  initialize(ctx, argc, argv);

  int c;
  while ((c = getopt_long(argc,
                          const_cast<char* const*>(argv),
                          "cCk:hF:M:po:sVxX:z",
                          options,
                          nullptr))
         != -1) {
    std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case DUMP_MANIFEST:
      return manifest_dump(arg, stdout) ? 0 : 1;

    case DUMP_RESULT: {
      ResultDumper result_dumper(stdout);
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_dumper);
      if (error) {
        fmt::print(stderr, "Error: {}\n", *error);
      }
      return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    case EVICT_OLDER_THAN: {
      auto seconds = Util::parse_duration(arg);
      ProgressBar progress_bar("Evicting...");
      clean_old(
        ctx, [&](double progress) { progress_bar.update(progress); }, seconds);
      if (isatty(STDOUT_FILENO)) {
        printf("\n");
      }
      break;
    }

    case EXTRACT_RESULT: {
      ResultExtractor result_extractor(".");
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_extractor);
      if (error) {
        fmt::print(stderr, "Error: {}\n", *error);
      }
      return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    case HASH_FILE: {
      Hash hash;
      if (arg == "-") {
        hash.hash_fd(STDIN_FILENO);
      } else {
        hash.hash_file(arg);
      }
      fmt::print("{}", hash.digest().to_string());
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
      wipe_all(ctx, [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        printf("\n");
      }
      break;
    }

    case 'h': // --help
      fmt::print(stdout, USAGE_TEXT, MYNAME, MYNAME);
      x_exit(0);

    case 'k': // --get-config
      fmt::print("{}\n", ctx.config.get_string_value(arg));
      break;

    case 'F': { // --max-files
      auto files = Util::parse_uint32(arg);
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_files", arg);
      if (files == 0) {
        printf("Unset cache file limit\n");
      } else {
        fmt::print("Set cache file limit to {}\n", files);
      }
      break;
    }

    case 'M': { // --max-size
      uint64_t size = Util::parse_size(arg);
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_size", arg);
      if (size == 0) {
        printf("Unset cache size limit\n");
      } else {
        fmt::print("Set cache size limit to {}\n",
                   Util::format_human_readable_size(size));
      }
      break;
    }

    case 'o': { // --set-config
      // Start searching for equal sign at position 1 to improve error message
      // for the -o=K=V case (key "=K" and value "V").
      size_t eq_pos = arg.find('=', 1);
      if (eq_pos == std::string::npos) {
        throw Error(fmt::format("missing equal sign in \"{}\"", arg));
      }
      std::string key = arg.substr(0, eq_pos);
      std::string value = arg.substr(eq_pos + 1);
      Config::set_value_in_file(ctx.config.primary_config_path(), key, value);
      break;
    }

    case 'p': // --show-config
      ctx.config.visit_items(configuration_printer);
      break;

    case 's': // --show-stats
      stats_summary(ctx);
      break;

    case 'V': // --version
      fprintf(stdout, VERSION_TEXT, MYNAME, CCACHE_VERSION);
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
      if (arg == "uncompressed") {
        level = 0;
      } else {
        level = Util::parse_int(arg);
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
      stats_zero(ctx);
      printf("Statistics zeroed\n");
      break;

    default:
      fmt::print(stderr, USAGE_TEXT, MYNAME, MYNAME);
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
    if (Util::same_program_name(program_name, MYNAME)) {
      if (argc < 2) {
        fmt::print(stderr, USAGE_TEXT, MYNAME, MYNAME);
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
