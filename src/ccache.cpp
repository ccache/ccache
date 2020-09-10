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
#include "Checksum.hpp"
#include "Compression.hpp"
#include "Context.hpp"
#include "Fd.hpp"
#include "File.hpp"
#include "FormatNonstdStringView.hpp"
#include "Hash.hpp"
#include "Logging.hpp"
#include "Manifest.hpp"
#include "MiniTrace.hpp"
#include "ProgressBar.hpp"
#include "Result.hpp"
#include "ResultDumper.hpp"
#include "ResultExtractor.hpp"
#include "ResultRetriever.hpp"
#include "SignalHandler.hpp"
#include "StdMakeUnique.hpp"
#include "TemporaryFile.hpp"
#include "UmaskScope.hpp"
#include "Util.hpp"
#include "argprocessing.hpp"
#include "cleanup.hpp"
#include "compopt.hpp"
#include "compress.hpp"
#include "exceptions.hpp"
#include "execute.hpp"
#include "hashutil.hpp"
#include "language.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#elif defined(_WIN32)
#  include "third_party/win32/getopt.h"
#else
#  include "third_party/getopt_long.h"
#endif

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#include <algorithm>
#include <limits>

#ifndef MYNAME
#  define MYNAME "ccache"
#endif
const char CCACHE_NAME[] = MYNAME;

using Logging::log;
using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

const char VERSION_TEXT[] =
  R"({} version {}

Copyright (C) 2002-2007 Andrew Tridgell
Copyright (C) 2009-2020 Joel Rosdahl and other contributors

See <https://ccache.dev/credits.html> for a complete list of contributors.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.
)";

const char USAGE_TEXT[] =
  R"(Usage:
    {} [options]
    {} compiler [compiler options]
    compiler [compiler options]          (via symbolic link)

Common options:
    -c, --cleanup              delete old files and recalculate size counters
                               (normally not needed as this is done
                               automatically)
    -C, --clear                clear the cache completely (except configuration)
    -d, --directory PATH       operate on cache directory PATH instead of the
                               default
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
        --checksum-file PATH   print the checksum (64 bit XXH3) of the file at
                               PATH
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
    std::string path = find_executable(ctx, word, CCACHE_NAME);
    if (path.empty()) {
      throw Fatal("{}: {}", word, strerror(errno));
    }

    prefix.push_back(path);
  }

  log("Using command-line prefix {}", prefix_command);
  for (size_t i = prefix.size(); i != 0; i--) {
    args.push_front(prefix[i - 1]);
  }
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

  Util::update_mtime(config.cache_dir());

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
                string_view obj_path,
                char type,
                string_view section_name,
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
    log("Failed to open {}: {}", path, strerror(errno));
  }
}

static GuessedCompiler
guess_compiler(string_view path)
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
    log("Non-regular include file {}", path);
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
    log("Include file {} too new", path);
    return false;
  }

  // The same >= logic as above applies to the change time of the file.
  if (!(ctx.config.sloppiness() & SLOPPY_INCLUDE_FILE_CTIME)
      && st.ctime() >= ctx.time_of_compilation) {
    log("Include file {} ctime too new", path);
    return false;
  }

  // Let's hash the include file content.
  Hash fhash;

  is_pch = Util::is_precompiled_header(path);
  if (is_pch) {
    if (ctx.included_pch_file.empty()) {
      log("Detected use of precompiled header: {}", path);
    }
    bool using_pch_sum = false;
    if (ctx.config.pch_external_checksum()) {
      // hash pch.sum instead of pch when it exists
      // to prevent hashing a very large .pch file every time
      std::string pch_sum_path = fmt::format("{}.sum", path);
      if (Stat::stat(pch_sum_path, Stat::OnError::log)) {
        path = std::move(pch_sum_path);
        using_pch_sum = true;
        log("Using pch.sum file {}", path);
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

// This function hashes an include file and stores the path and hash in
// ctx.included_files. If the include file is a PCH, cpp_hash is also updated.
static void
remember_include_file(Context& ctx,
                      const std::string& path,
                      Hash& cpp_hash,
                      bool system,
                      Hash* depend_mode_hash)
{
  if (!do_remember_include_file(ctx, path, cpp_hash, system, depend_mode_hash)
      && ctx.config.direct_mode()) {
    log("Disabling direct mode");
    ctx.config.set_direct_mode(false);
  }
}

static void
print_included_files(const Context& ctx, FILE* fp)
{
  for (const auto& item : ctx.included_files) {
    fmt::print(fp, "{}\n", item.first);
  }
}

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in ctx.included_files.
static bool
process_preprocessed_file(Context& ctx,
                          Hash& hash,
                          const std::string& path,
                          bool pump)
{
  std::string data;
  try {
    data = Util::read_file(path);
  } catch (Error&) {
    return false;
  }

  // Bytes between p and q are pending to be hashed.
  const char* p = &data[0];
  char* q = &data[0];
  const char* end = p + data.length();

  // There must be at least 7 characters (# 1 "x") left to potentially find an
  // include file path.
  while (q < end - 7) {
    static const string_view pragma_gcc_pch_preprocess =
      "pragma GCC pch_preprocess ";
    static const string_view hash_31_command_line_newline =
      "# 31 \"<command-line>\"\n";
    static const string_view hash_32_command_line_2_newline =
      "# 32 \"<command-line>\" 2\n";

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
            || Util::starts_with(&q[1], pragma_gcc_pch_preprocess)
            // HP/AIX:
            || (q[1] == 'l' && q[2] == 'i' && q[3] == 'n' && q[4] == 'e'
                && q[5] == ' '))
        && (q == data.data() || q[-1] == '\n')) {
      // Workarounds for preprocessor linemarker bugs in GCC version 6.
      if (q[2] == '3') {
        if (Util::starts_with(q, hash_31_command_line_newline)) {
          // Bogus extra line with #31, after the regular #1: Ignore the whole
          // line, and continue parsing.
          hash.hash(p, q - p);
          while (q < end && *q != '\n') {
            q++;
          }
          q++;
          p = q;
          continue;
        } else if (Util::starts_with(q, hash_32_command_line_2_newline)) {
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
        log("Failed to parse included file path");
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
      log(
        "Found unsupported .inc"
        "bin directive in source code");
      throw Failure(Statistic::unsupported_code_directive);
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
    std::string pch_path = Util::make_relative_path(ctx, ctx.included_pch_file);
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
    log("Base dir not set, skip using relative paths");
    return; // nothing to do
  }
  if (!ctx.has_absolute_include_headers) {
    log("No absolute path for included files found, skip using relative paths");
    return; // nothing to do
  }

  const std::string& output_dep = ctx.args_info.output_dep;
  std::string file_content;
  try {
    file_content = Util::read_file(output_dep);
  } catch (const Error& e) {
    log("Cannot open dependency file {}: {}", output_dep, e.what());
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
    log(
      "No paths in dependency file {} made relative, skip relative path usage",
      output_dep);
    return;
  }

  std::string tmp_file = output_dep + ".tmp";
  Util::write_file(tmp_file, adjusted_file_content);
  Util::rename(tmp_file, output_dep);
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
    log(
      "Cannot open dependency file {}: {}", ctx.args_info.output_dep, e.what());
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
    std::string pch_path = Util::make_relative_path(ctx, ctx.included_pch_file);
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
  UmaskScope umask_scope(ctx.original_umask);

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
      log("-fdiagnostics-color is unsupported; trying again without it");

      tmp_stdout.fd = Fd(open(
        tmp_stdout.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stdout.fd) {
        log("Failed to truncate {}: {}", tmp_stdout.path, strerror(errno));
        throw Failure(Statistic::internal_error);
      }

      tmp_stderr.fd = Fd(open(
        tmp_stderr.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stderr.fd) {
        log("Failed to truncate {}: {}", tmp_stderr.path, strerror(errno));
        throw Failure(Statistic::internal_error);
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
  log("Adding result name to {}", ctx.manifest_path());
  if (!Manifest::put(ctx.config,
                     ctx.manifest_path(),
                     ctx.result_name(),
                     ctx.included_files,
                     ctx.time_of_compilation,
                     save_timestamp)) {
    log("Failed to add result name to {}", ctx.manifest_path());
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
create_cachedir_tag(string_view dir)
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
  Util::unsetenv("DEPENDENCIES_OUTPUT");
  Util::unsetenv("SUNPRO_DEPENDENCIES");

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
      log("Failed to unlink {}: {}", ctx.args_info.output_dwo, strerror(errno));
      throw Failure(Statistic::bad_output_file);
    }
  }

  log("Running real compiler");
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
    throw Failure(Statistic::missing_cache_file);
  }

  // distcc-pump outputs lines like this:
  // __________Using # distcc servers in pump mode
  if (st.size() != 0 && ctx.guessed_compiler != GuessedCompiler::pump) {
    log("Compiler produced stdout");
    throw Failure(Statistic::compiler_produced_stdout);
  }

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler into tmp_stderr.
  if (!ctx.cpp_stderr.empty()) {
    std::string combined_stderr =
      Util::read_file(ctx.cpp_stderr) + Util::read_file(tmp_stderr_path);
    Util::write_file(tmp_stderr_path, combined_stderr);
  }

  if (status != 0) {
    log("Compiler gave exit status {}", status);

    // We can output stderr immediately instead of rerunning the compiler.
    Util::send_to_stderr(ctx, Util::read_file(tmp_stderr_path));

    throw Failure(Statistic::compile_failed, status);
  }

  if (ctx.config.depend_mode()) {
    assert(depend_mode_hash);
    auto result_name = result_name_from_depfile(ctx, *depend_mode_hash);
    if (!result_name) {
      throw Failure(Statistic::internal_error);
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
    log("Compiler didn't produce an object file");
    throw Failure(Statistic::compiler_produced_no_output);
  }
  if (st.size() == 0) {
    log("Compiler produced an empty object file");
    throw Failure(Statistic::compiler_produced_empty_output);
  }

  st = Stat::stat(tmp_stderr_path, Stat::OnError::log);
  if (!st) {
    throw Failure(Statistic::internal_error);
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
    log("Error: {}", *error);
  } else {
    log("Stored in cache: {}", ctx.result_path());
  }

  auto new_dest_stat = Stat::stat(ctx.result_path(), Stat::OnError::log);
  if (!new_dest_stat) {
    throw Failure(Statistic::internal_error);
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
    log("Failed to create {}/CACHEDIR.TAG ({})",
        first_level_dir,
        strerror(errno));
  }

  // Everything OK.
  Util::send_to_stderr(ctx, Util::read_file(tmp_stderr_path));
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
    log("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    status =
      do_execute(ctx, args, std::move(tmp_stdout), std::move(tmp_stderr));
    MTR_END("execute", "preprocessor");
    args.pop_back(args_added);
  }

  if (status != 0) {
    log("Preprocessor gave exit status {}", status);
    throw Failure(Statistic::preprocessor_error);
  }

  hash.hash_delimiter("cpp");
  bool is_pump = ctx.guessed_compiler == GuessedCompiler::pump;
  if (!process_preprocessed_file(ctx, hash, stdout_path, is_pump)) {
    throw Failure(Statistic::internal_error);
  }

  hash.hash_delimiter("cppstderr");
  if (!ctx.args_info.direct_i_file && !hash.hash_file(stderr_path)) {
    // Somebody removed the temporary file?
    log("Failed to open {}: {}", stderr_path, strerror(errno));
    throw Failure(Statistic::internal_error);
  }

  if (ctx.args_info.direct_i_file) {
    ctx.i_tmpfile = ctx.args_info.input_file;
  } else {
    // i_tmpfile needs the proper cpp_extension for the compiler to do its
    // thing correctly
    ctx.i_tmpfile =
      fmt::format("{}.{}", stdout_path, ctx.config.cpp_extension());
    Util::rename(stdout_path, ctx.i_tmpfile);
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
              const std::string& path,
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
    hash.hash(&ctx.config.compiler_check()[7]);
  } else if (ctx.config.compiler_check() == "content" || !allow_command) {
    hash.hash_delimiter("cc_content");
    hash_binary_file(ctx, hash, path);
  } else { // command string
    if (!hash_multicommand_output(
          hash, ctx.config.compiler_check(), ctx.orig_args[0])) {
      log("Failure running compiler check command: {}",
          ctx.config.compiler_check());
      throw Failure(Statistic::compiler_check_failed);
    }
  }
}

// Hash the host compiler(s) invoked by nvcc.
//
// If `ccbin_st` and `ccbin` are set, they refer to a directory or compiler set
// with -ccbin/--compiler-bindir. If `ccbin_st` is nullptr or `ccbin` is the
// empty string, the compilers are looked up in PATH instead.
static void
hash_nvcc_host_compiler(const Context& ctx,
                        Hash& hash,
                        const Stat* ccbin_st = nullptr,
                        const std::string& ccbin = {})
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

  if (ccbin.empty() || !ccbin_st || ccbin_st->is_directory()) {
#if defined(__APPLE__)
    const char* compilers[] = {"clang", "clang++"};
#elif defined(_WIN32)
    const char* compilers[] = {"cl.exe"};
#else
    const char* compilers[] = {"gcc", "g++"};
#endif
    for (const char* compiler : compilers) {
      if (!ccbin.empty()) {
        std::string path = fmt::format("{}/{}", ccbin, compiler);
        auto st = Stat::stat(path);
        if (st) {
          hash_compiler(ctx, hash, st, path, false);
        }
      } else {
        std::string path = find_executable(ctx, compiler, CCACHE_NAME);
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
  hash.hash(ctx.config.cpp_extension());

#ifdef _WIN32
  const std::string compiler_path = Win32Util::add_exe_suffix(args[0]);
#else
  const std::string compiler_path = args[0];
#endif

  auto st = Stat::stat(compiler_path, Stat::OnError::log);
  if (!st) {
    throw Failure(Statistic::could_not_find_compiler);
  }

  // Hash information about the compiler.
  hash_compiler(ctx, hash, st, compiler_path, true);

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
      const char* v = getenv(*p);
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
        log("Relocating debuginfo from {} to {} (CWD: {})",
            old_path,
            new_path,
            ctx.apparent_cwd);
        if (Util::starts_with(ctx.apparent_cwd, old_path)) {
          dir_to_hash = new_path + ctx.apparent_cwd.substr(old_path.size());
        }
      }
    }
    log("Hashing CWD {}", dir_to_hash);
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
    log("Hashing coverage path {}", gcda_path);
    hash.hash_delimiter("gcda");
    hash.hash(gcda_path);
  }

  // Possibly hash the sanitize blacklist file path.
  for (const auto& sanitize_blacklist : args_info.sanitize_blacklists) {
    log("Hashing sanitize blacklist {}", sanitize_blacklist);
    hash.hash("sanitizeblacklist");
    if (!hash_binary_file(ctx, hash, sanitize_blacklist)) {
      throw Failure(Statistic::error_hashing_extra_file);
    }
  }

  if (!ctx.config.extra_files_to_hash().empty()) {
    for (const std::string& path : Util::split_into_strings(
           ctx.config.extra_files_to_hash(), PATH_DELIM)) {
      log("Hashing extra file {}", path);
      hash.hash_delimiter("extrafile");
      if (!hash_binary_file(ctx, hash, path)) {
        throw Failure(Statistic::error_hashing_extra_file);
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
    log("Checking for profile data file {}", p);
    auto st = Stat::stat(p);
    if (st && !st.is_directory()) {
      log("Adding profile data {} to the hash", p);
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
    hash.hash(Manifest::k_version);
  }

  // clang will emit warnings for unused linker flags, so we shouldn't skip
  // those arguments.
  int is_clang = ctx.guessed_compiler == GuessedCompiler::clang
                 || ctx.guessed_compiler == GuessedCompiler::unknown;

  // First the arguments.
  for (size_t i = 1; i < args.size(); i++) {
    // Trust the user if they've said we should not hash a given option.
    if (option_should_be_ignored(args[i], ctx.ignore_options())) {
      log("Not hashing ignored option: {}", args[i]);
      if (i + 1 < args.size() && compopt_takes_arg(args[i])) {
        i++;
        log("Not hashing argument of ignored option: {}", args[i]);
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
            && args[i].find(',', 8) == std::string::npos) {
          hash.hash(args[i].data(), 8);
          continue;
        } else if (Util::starts_with(args[i], "-Wp,-MMD,")
                   && args[i].find(',', 9) == std::string::npos) {
          hash.hash(args[i].data(), 9);
          continue;
        }
      } else if (Util::starts_with(args[i], "-MF")) {
        // In either case, hash the "-MF" part.
        hash.hash_delimiter("arg");
        hash.hash(args[i].data(), 3);

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

    if (Util::starts_with(args[i], "-specs=")
        || Util::starts_with(args[i], "--specs=")) {
      std::string path = args[i].substr(args[i].find('=') + 1);
      auto st = Stat::stat(path, Stat::OnError::log);
      if (st) {
        // If given an explicit specs file, then hash that file, but don't
        // include the path to it in the hash.
        hash.hash_delimiter("specs");
        hash_compiler(ctx, hash, st, path, false);
        continue;
      }
    }

    if (Util::starts_with(args[i], "-fplugin=")) {
      auto st = Stat::stat(&args[i][9], Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        hash_compiler(ctx, hash, st, &args[i][9], false);
        continue;
      }
    }

    if (args[i] == "-Xclang" && i + 3 < args.size() && args[i + 1] == "-load"
        && args[i + 2] == "-Xclang") {
      auto st = Stat::stat(args[i + 3], Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        hash_compiler(ctx, hash, st, args[i + 3], false);
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
        hash_nvcc_host_compiler(ctx, hash, &st, args[i + 1]);
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
    hash_nvcc_host_compiler(ctx, hash);
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
    log("Adding profile directory {} to our hash", ctx.args_info.profile_path);
    hash.hash_delimiter("-fprofile-dir");
    hash.hash(ctx.args_info.profile_path);
  }

  if (ctx.args_info.profile_use && !hash_profile_data_file(ctx, hash)) {
    log("No profile data file found");
    throw Failure(Statistic::no_input_file);
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
      const char* v = getenv(*p);
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
      throw Failure(Statistic::internal_error);
    }
    if (result & HASH_SOURCE_CODE_FOUND_TIME) {
      log("Disabling direct mode");
      ctx.config.set_direct_mode(false);
      return nullopt;
    }

    ctx.set_manifest_name(hash.digest());

    log("Looking for result name in {}", ctx.manifest_path());
    MTR_BEGIN("manifest", "manifest_get");
    result_name = Manifest::get(ctx, ctx.manifest_path());
    MTR_END("manifest", "manifest_get");
    if (result_name) {
      log("Got result name from manifest");
    } else {
      log("Did not find result name in manifest");
    }
  } else {
    if (ctx.args_info.arch_args.empty()) {
      result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
      log("Got result name from preprocessor");
    } else {
      preprocessor_args.push_back("-arch");
      for (size_t i = 0; i < ctx.args_info.arch_args.size(); ++i) {
        preprocessor_args.push_back(ctx.args_info.arch_args[i]);
        result_name = get_result_name_from_cpp(ctx, preprocessor_args, hash);
        log("Got result name from preprocessor with -arch {}",
            ctx.args_info.arch_args[i]);
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

enum class FromCacheCallMode { direct, cpp };

// Try to return the compile result from cache.
static optional<Statistic>
from_cache(Context& ctx, FromCacheCallMode mode)
{
  UmaskScope umask_scope(ctx.original_umask);

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
      && !ctx.args_info.fno_pch_timestamp && mode == FromCacheCallMode::cpp) {
    log("Not considering cached precompiled header in preprocessor mode");
    return nullopt;
  }

  MTR_BEGIN("cache", "from_cache");

  // Get result from cache.
  Result::Reader result_reader(ctx.result_path());
  ResultRetriever result_retriever(
    ctx, should_rewrite_dependency_target(ctx.args_info));

  auto error = result_reader.read(result_retriever);
  if (error) {
    log("Failed to get result from cache: {}", *error);
    return nullopt;
  } else {
    // Update modification timestamp to save file from LRU cleanup.
    Util::update_mtime(ctx.result_path());
  }

  log("Succeeded getting cached result");

  MTR_END("cache", "from_cache");

  return mode == FromCacheCallMode::direct ? Statistic::direct_cache_hit
                                           : Statistic::preprocessed_cache_hit;
}

// Find the real compiler. We just search the PATH to find an executable of the
// same name that isn't a link to ourselves.
static void
find_compiler(Context& ctx, const char* const* argv)
{
  // We might be being invoked like "ccache gcc -c foo.c".
  std::string base(Util::base_name(argv[0]));
  if (Util::same_program_name(base, CCACHE_NAME)) {
    ctx.orig_args.pop_front();
    if (Util::is_full_path(ctx.orig_args[0])) {
      return;
    }
    base = std::string(Util::base_name(ctx.orig_args[0]));
  }

  // Support user override of the compiler.
  if (!ctx.config.compiler().empty()) {
    base = ctx.config.compiler();
  }

  std::string compiler = find_executable(ctx, base, CCACHE_NAME);
  if (compiler.empty()) {
    throw Fatal("Could not find compiler \"{}\" in PATH", base);
  }
  if (compiler == argv[0]) {
    throw Fatal(
      "Recursive invocation (the name of the ccache binary must be \"{}\")",
      CCACHE_NAME);
  }
  ctx.orig_args[0] = compiler;
}

static void
create_initial_config_file(Config& config)
{
  if (!Util::create_dir(Util::dir_name(config.primary_config_path()))) {
    return;
  }

  uint64_t max_files;
  uint64_t max_size;
  std::string stats_dir = fmt::format("{}/0", config.cache_dir());
  if (Stat::stat(stats_dir)) {
    stats_get_obsolete_limits(stats_dir, &max_files, &max_size);
    // Max files and max size were stored for each top directory.
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
    fmt::print(f, "max_files = {}\n", max_files);
    config.set_max_files(max_files);
  }
  if (max_size != 0) {
    std::string size = Util::format_parsable_size_with_suffix(max_size);
    fmt::print(f, "max_size = {}\n", size);
    config.set_max_size(max_size);
  }
  fclose(f);
}

static std::string
default_cache_dir(const std::string& home_dir)
{
#ifdef _WIN32
  return home_dir + "/ccache";
#elif defined(__APPLE__)
  return home_dir + "/Library/Caches/ccache";
#else
  return home_dir + "/.cache/ccache";
#endif
}

static std::string
default_config_dir(const std::string& home_dir)
{
#ifdef _WIN32
  return home_dir + "/ccache";
#elif defined(__APPLE__)
  return home_dir + "/Library/Preferences/ccache";
#else
  return home_dir + "/.config/ccache";
#endif
}

// Read config file(s), populate variables, create configuration file in cache
// directory if missing, etc. Returns whether the primary configuration file
// exists.
static bool
set_up_config(Config& config)
{
  const std::string home_dir = Util::get_home_directory();
  const std::string legacy_ccache_dir = home_dir + "/.ccache";
  const bool legacy_ccache_dir_exists =
    Stat::stat(legacy_ccache_dir).is_directory();
  const char* const env_xdg_cache_home = getenv("XDG_CACHE_HOME");
  const char* const env_xdg_config_home = getenv("XDG_CONFIG_HOME");

  const char* env_ccache_configpath = getenv("CCACHE_CONFIGPATH");
  if (env_ccache_configpath) {
    config.set_primary_config_path(env_ccache_configpath);
  } else {
    // Only used for ccache tests:
    const char* const env_ccache_configpath2 = getenv("CCACHE_CONFIGPATH2");

    config.set_secondary_config_path(
      env_ccache_configpath2 ? env_ccache_configpath2
                             : fmt::format("{}/ccache.conf", SYSCONFDIR));
    MTR_BEGIN("config", "conf_read_secondary");
    // A missing config file in SYSCONFDIR is OK so don't check return value.
    config.update_from_file(config.secondary_config_path());
    MTR_END("config", "conf_read_secondary");

    const char* const env_ccache_dir = getenv("CCACHE_DIR");
    std::string primary_config_dir;
    if (env_ccache_dir && *env_ccache_dir) {
      primary_config_dir = env_ccache_dir;
    } else if (!config.cache_dir().empty() && !env_ccache_dir) {
      primary_config_dir = config.cache_dir();
    } else if (legacy_ccache_dir_exists) {
      primary_config_dir = legacy_ccache_dir;
    } else if (env_xdg_config_home) {
      primary_config_dir = fmt::format("{}/ccache", env_xdg_config_home);
    } else {
      primary_config_dir = default_config_dir(home_dir);
    }
    config.set_primary_config_path(primary_config_dir + "/ccache.conf");
  }

  const std::string& cache_dir_before_primary_config = config.cache_dir();

  MTR_BEGIN("config", "conf_read_primary");
  const bool primary_config_exists =
    config.update_from_file(config.primary_config_path());
  MTR_END("config", "conf_read_primary");

  // Ignore cache_dir set in primary config.
  config.set_cache_dir(cache_dir_before_primary_config);

  MTR_BEGIN("config", "conf_update_from_environment");
  config.update_from_environment();
  // (config.cache_dir is set above if CCACHE_DIR is set.)
  MTR_END("config", "conf_update_from_environment");

  if (config.cache_dir().empty()) {
    if (legacy_ccache_dir_exists) {
      config.set_cache_dir(legacy_ccache_dir);
    } else if (env_xdg_cache_home) {
      config.set_cache_dir(fmt::format("{}/ccache", env_xdg_cache_home));
    } else {
      config.set_cache_dir(default_cache_dir(home_dir));
    }
  }
  // else: cache_dir was set explicitly via environment or via secondary config.

  // We have now determined config.cache_dir and populated the rest of config in
  // prio order (1. environment, 2. primary config, 3. secondary config).

  return primary_config_exists;
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
  bool primary_config_exists = set_up_config(ctx.config);
  set_up_context(ctx, argc, argv);
  Logging::init(ctx.config);

  // Set default umask for all files created by ccache from now on (if
  // configured to). This is intentionally done after calling init_log so that
  // the log file won't be affected by the umask but before creating the initial
  // configuration file. The intention is that all files and directories in the
  // cache directory should be affected by the configured umask and that no
  // other files and directories should.
  if (ctx.config.umask() != std::numeric_limits<uint32_t>::max()) {
    ctx.original_umask = umask(ctx.config.umask());
  }

  if (!primary_config_exists && !ctx.config.disable()) {
    create_initial_config_file(ctx.config);
  }

  log("=== CCACHE {} STARTED =========================================",
      CCACHE_VERSION);

  if (getenv("CCACHE_INTERNAL_TRACE")) {
#ifdef MTR_ENABLED
    ctx.mini_trace = std::make_unique<MiniTrace>(ctx.args_info);
#else
    log("Error: tracing is not enabled!");
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
    log("dup(2) failed: {}", strerror(errno));
    throw Failure(Statistic::internal_error);
  }

  Util::setenv("UNCACHED_ERR_FD", fmt::format("{}", uncached_fd));
}

static void
configuration_logger(const std::string& key,
                     const std::string& value,
                     const std::string& origin)
{
  Logging::bulk_log("Config: ({}) {} = {}", origin, key, value);
}

static void
configuration_printer(const std::string& key,
                      const std::string& value,
                      const std::string& origin)
{
  fmt::print("({}) {} = {}\n", origin, key, value);
}

static int cache_compilation(int argc, const char* const* argv);
static Statistic do_cache_compilation(Context& ctx, const char* const* argv);

// The entry point when invoked to cache a compilation.
static int
cache_compilation(int argc, const char* const* argv)
{
  tzset(); // Needed for localtime_r.

  auto ctx = std::make_unique<Context>();
  SignalHandler signal_handler(*ctx);

  initialize(*ctx, argc, argv);

  MTR_BEGIN("main", "find_compiler");
  find_compiler(*ctx, argv);
  MTR_END("main", "find_compiler");

  try {
    Statistic statistic = do_cache_compilation(*ctx, argv);
    ctx->counter_updates[statistic] += 1;
    return EXIT_SUCCESS;
  } catch (const Failure& e) {
    if (e.statistic() != Statistic::none) {
      ctx->counter_updates[e.statistic()] += 1;
    }

    if (e.exit_code()) {
      return *e.exit_code();
    }
    // Else: Fall back to running the real compiler.

    if (ctx->original_umask) {
      umask(*ctx->original_umask);
    }

    assert(!ctx->orig_args.empty());

    ctx->orig_args.erase_with_prefix("--ccache-");
    add_prefix(*ctx, ctx->orig_args, ctx->config.prefix_command());

    log("Failed; falling back to running the real compiler");

    Args saved_orig_args(std::move(ctx->orig_args));
    auto execv_argv = saved_orig_args.to_argv();

    log("Executing {}", Util::format_argv_for_logging(execv_argv.data()));
    ctx.reset(); // Dump debug logs last thing before executing.
    execv(execv_argv[0], const_cast<char* const*>(execv_argv.data()));
    throw Fatal("execv of {} failed: {}", execv_argv[0], strerror(errno));
  }
}

static Statistic
do_cache_compilation(Context& ctx, const char* const* argv)
{
  if (ctx.actual_cwd.empty()) {
    log("Unable to determine current working directory: {}", strerror(errno));
    throw Failure(Statistic::internal_error);
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
    log("ccache is disabled");
    // Statistic::cache_miss is a dummy to trigger stats_flush.
    throw Failure(Statistic::cache_miss);
  }

  MTR_BEGIN("main", "set_up_uncached_err");
  set_up_uncached_err();
  MTR_END("main", "set_up_uncached_err");

  log("Command line: {}", Util::format_argv_for_logging(argv));
  log("Hostname: {}", Util::get_hostname());
  log("Working directory: {}", ctx.actual_cwd);
  if (ctx.apparent_cwd != ctx.actual_cwd) {
    log("Apparent working directory: {}", ctx.apparent_cwd);
  }

  ctx.config.set_limit_multiple(
    std::min(std::max(ctx.config.limit_multiple(), 0.0), 1.0));

  MTR_BEGIN("main", "guess_compiler");
  ctx.guessed_compiler = guess_compiler(ctx.orig_args[0]);
  MTR_END("main", "guess_compiler");

  MTR_BEGIN("main", "process_args");

  ProcessArgsResult processed = process_args(ctx);
  if (processed.error) {
    throw Failure(*processed.error);
  }

  MTR_END("main", "process_args");

  if (ctx.config.depend_mode()
      && (!ctx.args_info.generating_dependencies
          || ctx.args_info.output_dep == "/dev/null"
          || !ctx.config.run_second_cpp())) {
    log("Disabling depend mode");
    ctx.config.set_depend_mode(false);
  }

  log("Source file: {}", ctx.args_info.input_file);
  if (ctx.args_info.generating_dependencies) {
    log("Dependency file: {}", ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    log("Coverage file: {}", ctx.args_info.output_cov);
  }
  if (ctx.args_info.generating_stackusage) {
    log("Stack usage file: {}", ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    log("Diagnostics file: {}", ctx.args_info.output_dia);
  }
  if (!ctx.args_info.output_dwo.empty()) {
    log("Split dwarf file: {}", ctx.args_info.output_dwo);
  }

  log("Object file: {}", ctx.args_info.output_obj);
  MTR_META_THREAD_NAME(ctx.args_info.output_obj.c_str());

  if (ctx.config.debug()) {
    std::string path =
      fmt::format("{}.ccache-input-text", ctx.args_info.output_obj);
    File debug_text_file(path, "w");
    if (debug_text_file) {
      ctx.hash_debug_files.push_back(std::move(debug_text_file));
    } else {
      log("Failed to open {}: {}", path, strerror(errno));
    }
  }

  FILE* debug_text_file = !ctx.hash_debug_files.empty()
                            ? ctx.hash_debug_files.front().get()
                            : nullptr;

  Hash common_hash;
  init_hash_debug(
    ctx, common_hash, ctx.args_info.output_obj, 'c', "COMMON", debug_text_file);

  MTR_BEGIN("hash", "common_hash");
  hash_common_info(
    ctx, processed.preprocessor_args, common_hash, ctx.args_info);
  MTR_END("hash", "common_hash");

  // Try to find the hash using the manifest.
  Hash direct_hash = common_hash;
  init_hash_debug(ctx,
                  direct_hash,
                  ctx.args_info.output_obj,
                  'd',
                  "DIRECT MODE",
                  debug_text_file);

  Args args_to_hash = processed.preprocessor_args;
  args_to_hash.push_back(processed.extra_args_to_hash);

  bool put_result_in_manifest = false;
  optional<Digest> result_name;
  optional<Digest> result_name_from_manifest;
  if (ctx.config.direct_mode()) {
    log("Trying direct lookup");
    MTR_BEGIN("hash", "direct_hash");
    Args dummy_args;
    result_name =
      calculate_result_name(ctx, args_to_hash, dummy_args, direct_hash, true);
    MTR_END("hash", "direct_hash");
    if (result_name) {
      ctx.set_result_name(*result_name);

      // If we can return from cache at this point then do so.
      auto result = from_cache(ctx, FromCacheCallMode::direct);
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
    log("Read-only direct mode; running real compiler");
    throw Failure(Statistic::cache_miss);
  }

  if (!ctx.config.depend_mode()) {
    // Find the hash using the preprocessed output. Also updates
    // ctx.included_files.
    Hash cpp_hash = common_hash;
    init_hash_debug(ctx,
                    cpp_hash,
                    ctx.args_info.output_obj,
                    'p',
                    "PREPROCESSOR MODE",
                    debug_text_file);

    MTR_BEGIN("hash", "cpp_hash");
    result_name = calculate_result_name(
      ctx, args_to_hash, processed.preprocessor_args, cpp_hash, false);
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
      log("Hash from manifest doesn't match preprocessor output");
      log("Likely reason: different CCACHE_BASEDIRs used");
      log("Removing manifest as a safety measure");
      Util::unlink_safe(ctx.manifest_path());

      put_result_in_manifest = true;
    }

    // If we can return from cache at this point then do.
    auto result = from_cache(ctx, FromCacheCallMode::cpp);
    if (result) {
      if (put_result_in_manifest) {
        update_manifest_file(ctx);
      }
      return *result;
    }
  }

  if (ctx.config.read_only()) {
    log("Read-only mode; running real compiler");
    throw Failure(Statistic::cache_miss);
  }

  add_prefix(ctx, processed.compiler_args, ctx.config.prefix_command());

  // In depend_mode, extend the direct hash.
  Hash* depend_mode_hash = ctx.config.depend_mode() ? &direct_hash : nullptr;

  // Run real compiler, sending output to cache.
  MTR_BEGIN("cache", "to_cache");
  to_cache(ctx,
           processed.compiler_args,
           ctx.args_info.depend_extra_args,
           depend_mode_hash);
  update_manifest_file(ctx);
  MTR_END("cache", "to_cache");

  return Statistic::cache_miss;
}

// The main program when not doing a compile.
static int
handle_main_options(int argc, const char* const* argv)
{
  enum longopts {
    CHECKSUM_FILE,
    DUMP_MANIFEST,
    DUMP_RESULT,
    EVICT_OLDER_THAN,
    EXTRACT_RESULT,
    HASH_FILE,
    PRINT_STATS,
  };
  static const struct option options[] = {
    {"checksum-file", required_argument, nullptr, CHECKSUM_FILE},
    {"cleanup", no_argument, nullptr, 'c'},
    {"clear", no_argument, nullptr, 'C'},
    {"directory", no_argument, nullptr, 'd'},
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
                          "cCd:k:hF:M:po:sVxX:z",
                          options,
                          nullptr))
         != -1) {
    std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case CHECKSUM_FILE: {
      Checksum checksum;
      Fd fd(arg == "-" ? STDIN_FILENO : open(arg.c_str(), O_RDONLY));
      Util::read_fd(*fd, [&checksum](const void* data, size_t size) {
        checksum.update(data, size);
      });
      fmt::print("{:016x}\n", checksum.digest());
      break;
    }

    case DUMP_MANIFEST:
      return Manifest::dump(arg, stdout) ? 0 : 1;

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
        fmt::print("\n");
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
      fmt::print("{}\n", hash.digest().to_string());
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
        fmt::print("\n");
      }
      break;
    }

    case 'C': // --clear
    {
      ProgressBar progress_bar("Clearing...");
      wipe_all(ctx, [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        fmt::print("\n");
      }
      break;
    }

    case 'd': // --directory
      Util::setenv("CCACHE_DIR", arg);
      break;

    case 'h': // --help
      fmt::print(stdout, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_SUCCESS);

    case 'k': // --get-config
      fmt::print("{}\n", ctx.config.get_string_value(arg));
      break;

    case 'F': { // --max-files
      auto files = Util::parse_unsigned(arg);
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_files", arg);
      if (files == 0) {
        fmt::print("Unset cache file limit\n");
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
        fmt::print("Unset cache size limit\n");
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
        throw Error("missing equal sign in \"{}\"", arg);
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
      fmt::print(VERSION_TEXT, CCACHE_NAME, CCACHE_VERSION);
      exit(EXIT_SUCCESS);

    case 'x': // --show-compression
    {
      ProgressBar progress_bar("Scanning...");
      compress_stats(ctx.config,
                     [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'X': // --recompress
    {
      optional<int8_t> wanted_level;
      if (arg == "uncompressed") {
        wanted_level = nullopt;
      } else {
        wanted_level =
          Util::parse_signed(arg, INT8_MIN, INT8_MAX, "compression level");
      }

      ProgressBar progress_bar("Recompressing...");
      compress_recompress(ctx, wanted_level, [&](double progress) {
        progress_bar.update(progress);
      });
      break;
    }

    case 'z': // --zero-stats
      stats_zero(ctx);
      fmt::print("Statistics zeroed\n");
      break;

    default:
      fmt::print(stderr, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_FAILURE);
    }

    // Some of the above switches might have changed config settings, so run the
    // setup again.
    ctx.config = Config();
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
    if (Util::same_program_name(program_name, CCACHE_NAME)) {
      if (argc < 2) {
        fmt::print(stderr, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
        exit(EXIT_FAILURE);
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
