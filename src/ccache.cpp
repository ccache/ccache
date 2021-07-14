// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2021 Joel Rosdahl and other contributors
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
#include "Depfile.hpp"
#include "Fd.hpp"
#include "File.hpp"
#include "Finalizer.hpp"
#include "FormatNonstdStringView.hpp"
#include "Hash.hpp"
#include "Lockfile.hpp"
#include "Logging.hpp"
#include "Manifest.hpp"
#include "MiniTrace.hpp"
#include "ProgressBar.hpp"
#include "Result.hpp"
#include "ResultDumper.hpp"
#include "ResultExtractor.hpp"
#include "ResultRetriever.hpp"
#include "SignalHandler.hpp"
#include "Statistics.hpp"
#include "TemporaryFile.hpp"
#include "UmaskScope.hpp"
#include "Util.hpp"
#include "Win32Util.hpp"
#include "argprocessing.hpp"
#include "cleanup.hpp"
#include "compopt.hpp"
#include "compress.hpp"
#include "exceptions.hpp"
#include "execute.hpp"
#include "fmtmacros.hpp"
#include "hashutil.hpp"
#include "language.hpp"

#include <core/types.hpp>
#include <core/wincompat.hpp>
#include <util/path_utils.hpp>

#include "third_party/fmt/core.h"
#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#elif defined(_WIN32)
#  include "third_party/win32/getopt.h"
#else
extern "C" {
#  include "third_party/getopt_long.h"
}
#endif

#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#ifndef MYNAME
#  define MYNAME "ccache"
#endif
const char CCACHE_NAME[] = MYNAME;

using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

constexpr const char VERSION_TEXT[] =
  R"({} version {}
Features: {}

Copyright (C) 2002-2007 Andrew Tridgell
Copyright (C) 2009-2021 Joel Rosdahl and other contributors

See <https://ccache.dev/credits.html> for a complete list of contributors.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.
)";

constexpr const char USAGE_TEXT[] =
  R"(Usage:
    {} [options]
    {} compiler [compiler options]
    compiler [compiler options]          (via symbolic link)

Common options:
    -c, --cleanup              delete old files and recalculate size counters
                               (normally not needed as this is done
                               automatically)
    -C, --clear                clear the cache completely (except configuration)
        --config-path PATH     operate on configuration file PATH instead of the
                               default
    -d, --directory PATH       operate on cache directory PATH instead of the
                               default
        --evict-older-than AGE remove files older than AGE (unsigned integer
                               with a d (days) or s (seconds) suffix)
    -F, --max-files NUM        set maximum number of files in cache to NUM (use
                               0 for no limit)
    -M, --max-size SIZE        set maximum size of cache to SIZE (use 0 for no
                               limit); available suffixes: k, M, G, T (decimal)
                               and Ki, Mi, Gi, Ti (binary); default suffix: G
    -X, --recompress LEVEL     recompress the cache to level LEVEL (integer or
                               "uncompressed") using the Zstandard algorithm;
                               see "Cache compression" in the manual for details
    -o, --set-config KEY=VAL   set configuration item KEY to value VAL
    -x, --show-compression     show compression statistics
    -p, --show-config          show current configuration options in
                               human-readable format
        --show-log-stats       print statistics counters from the stats log
                               in human-readable format
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

See also the manual on <https://ccache.dev/documentation.html>.
)";

constexpr const char FEATURE_TEXT[] =
  "http-storage"
#ifdef HAVE_REDIS_STORAGE_BACKEND
  " redis-storage"
#endif
  ;

// This is a string that identifies the current "version" of the hash sum
// computed by ccache. If, for any reason, we want to force the hash sum to be
// different for the same input in a new ccache version, we can just change
// this string. A typical example would be if the format of one of the files
// stored in the cache changes in a backwards-incompatible way.
const char HASH_PREFIX[] = "3";

namespace {

// Throw a Failure if ccache did not succeed in getting or putting a result in
// the cache. If `exit_code` is set, just exit with that code directly,
// otherwise execute the real compiler and exit with its exit code. Also updates
// statistics counter `statistic` if it's not `Statistic::none`.
class Failure : public std::exception
{
public:
  Failure(Statistic statistic,
          nonstd::optional<int> exit_code = nonstd::nullopt);

  nonstd::optional<int> exit_code() const;
  Statistic statistic() const;

private:
  Statistic m_statistic;
  nonstd::optional<int> m_exit_code;
};

inline Failure::Failure(Statistic statistic, nonstd::optional<int> exit_code)
  : m_statistic(statistic),
    m_exit_code(exit_code)
{
}

inline nonstd::optional<int>
Failure::exit_code() const
{
  return m_exit_code;
}

inline Statistic
Failure::statistic() const
{
  return m_statistic;
}

} // namespace

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

  LOG("Using command-line prefix {}", prefix_command);
  for (size_t i = prefix.size(); i != 0; i--) {
    args.push_front(prefix[i - 1]);
  }
}

static std::string
prepare_debug_path(const std::string& debug_dir,
                   const std::string& output_obj,
                   string_view suffix)
{
  auto prefix = debug_dir.empty()
                  ? output_obj
                  : debug_dir + util::to_absolute_path(output_obj);
#ifdef _WIN32
  prefix.erase(std::remove(prefix.begin(), prefix.end(), ':'), prefix.end());
#endif
  try {
    Util::ensure_dir_exists(Util::dir_name(prefix));
  } catch (Error&) {
    // Ignore since we can't handle an error in another way in this context. The
    // caller takes care of logging when trying to open the path for writing.
  }
  return FMT("{}.ccache-{}", prefix, suffix);
}

static void
init_hash_debug(Context& ctx,
                Hash& hash,
                char type,
                string_view section_name,
                FILE* debug_text_file)
{
  if (!ctx.config.debug()) {
    return;
  }

  const auto path = prepare_debug_path(
    ctx.config.debug_dir(), ctx.args_info.output_obj, FMT("input-{}", type));
  File debug_binary_file(path, "wb");
  if (debug_binary_file) {
    hash.enable_debug(section_name, debug_binary_file.get(), debug_text_file);
    ctx.hash_debug_files.push_back(std::move(debug_binary_file));
  } else {
    LOG("Failed to open {}: {}", path, strerror(errno));
  }
}

CompilerType
guess_compiler(string_view path)
{
  std::string compiler_path(path);

#ifndef _WIN32
  // Follow symlinks to the real compiler to learn its name. We're not using
  // Util::real_path in order to save some unnecessary stat calls.
  while (true) {
    std::string symlink_value = Util::read_link(compiler_path);
    if (symlink_value.empty()) {
      break;
    }
    if (util::is_absolute_path(symlink_value)) {
      compiler_path = symlink_value;
    } else {
      compiler_path =
        FMT("{}/{}", Util::dir_name(compiler_path), symlink_value);
    }
  }
#endif

  const string_view name = Util::base_name(compiler_path);
  if (name.find("clang") != nonstd::string_view::npos) {
    return CompilerType::clang;
  } else if (name.find("gcc") != nonstd::string_view::npos
             || name.find("g++") != nonstd::string_view::npos) {
    return CompilerType::gcc;
  } else if (name.find("nvcc") != nonstd::string_view::npos) {
    return CompilerType::nvcc;
  } else if (name == "pump" || name == "distcc-pump") {
    return CompilerType::pump;
  } else {
    return CompilerType::other;
  }
}

static bool
include_file_too_new(const Context& ctx,
                     const std::string& path,
                     const Stat& path_stat)
{
  // The comparison using >= is intentional, due to a possible race between
  // starting compilation and writing the include file. See also the notes under
  // "Performance" in doc/MANUAL.adoc.
  if (!(ctx.config.sloppiness() & SLOPPY_INCLUDE_FILE_MTIME)
      && path_stat.mtime() >= ctx.time_of_compilation) {
    LOG("Include file {} too new", path);
    return true;
  }

  // The same >= logic as above applies to the change time of the file.
  if (!(ctx.config.sloppiness() & SLOPPY_INCLUDE_FILE_CTIME)
      && path_stat.ctime() >= ctx.time_of_compilation) {
    LOG("Include file {} ctime too new", path);
    return true;
  }

  return false;
}

// Returns false if the include file was "too new" and therefore should disable
// the direct mode (or, in the case of a preprocessed header, fall back to just
// running the real compiler), otherwise true.
static bool
do_remember_include_file(Context& ctx,
                         std::string path,
                         Hash& cpp_hash,
                         bool system,
                         Hash* depend_mode_hash)
{
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

  // Canonicalize path for comparison; Clang uses ./header.h.
  if (Util::starts_with(path, "./")) {
    path.erase(0, 2);
  }

  if (ctx.included_files.find(path) != ctx.included_files.end()) {
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
    LOG("Non-regular include file {}", path);
    return false;
  }

  for (const auto& ignore_header_path : ctx.ignore_header_paths) {
    if (Util::matches_dir_prefix_or_file(ignore_header_path, path)) {
      return true;
    }
  }

  const bool is_pch = Util::is_precompiled_header(path);
  const bool too_new = include_file_too_new(ctx, path, st);

  if (too_new) {
    // Opt out of direct mode because of a race condition.
    //
    // The race condition consists of these events:
    //
    // - the preprocessor is run
    // - an include file is modified by someone
    // - the new include file is hashed by ccache
    // - the real compiler is run on the preprocessor's output, which contains
    //   data from the old header file
    // - the wrong object file is stored in the cache.

    return false;
  }

  // Let's hash the include file content.
  Hash fhash;

  if (is_pch) {
    if (ctx.included_pch_file.empty()) {
      LOG("Detected use of precompiled header: {}", path);
    }
    bool using_pch_sum = false;
    if (ctx.config.pch_external_checksum()) {
      // hash pch.sum instead of pch when it exists
      // to prevent hashing a very large .pch file every time
      std::string pch_sum_path = FMT("{}.sum", path);
      if (Stat::stat(pch_sum_path, Stat::OnError::log)) {
        path = std::move(pch_sum_path);
        using_pch_sum = true;
        LOG("Using pch.sum file {}", path);
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

enum class RememberIncludeFileResult { ok, cannot_use_pch };

// This function hashes an include file and stores the path and hash in
// ctx.included_files. If the include file is a PCH, cpp_hash is also updated.
static RememberIncludeFileResult
remember_include_file(Context& ctx,
                      const std::string& path,
                      Hash& cpp_hash,
                      bool system,
                      Hash* depend_mode_hash)
{
  if (!do_remember_include_file(
        ctx, path, cpp_hash, system, depend_mode_hash)) {
    if (Util::is_precompiled_header(path)) {
      return RememberIncludeFileResult::cannot_use_pch;
    } else if (ctx.config.direct_mode()) {
      LOG_RAW("Disabling direct mode");
      ctx.config.set_direct_mode(false);
    }
  }

  return RememberIncludeFileResult::ok;
}

static void
print_included_files(const Context& ctx, FILE* fp)
{
  for (const auto& item : ctx.included_files) {
    PRINT(fp, "{}\n", item.first);
  }
}

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in ctx.included_files.
//
// Returns Statistic::none on success, otherwise a statistics counter to be
// incremented.
static Statistic
process_preprocessed_file(Context& ctx,
                          Hash& hash,
                          const std::string& path,
                          bool pump)
{
  std::string data;
  try {
    data = Util::read_file(path);
  } catch (Error&) {
    return Statistic::internal_error;
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
        LOG_RAW("Failed to parse included file path");
        return Statistic::internal_error;
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
        ctx.has_absolute_include_headers = util::is_absolute_path(inc_path);
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

      if (remember_include_file(ctx, inc_path, hash, system, nullptr)
          == RememberIncludeFileResult::cannot_use_pch) {
        return Statistic::could_not_use_precompiled_header;
      }
      p = q; // Everything of interest between p and q has been hashed now.
    } else if (q[0] == '.' && q[1] == 'i' && q[2] == 'n' && q[3] == 'c'
               && q[4] == 'b' && q[5] == 'i' && q[6] == 'n') {
      // An assembler .inc bin (without the space) statement, which could be
      // part of inline assembly, refers to an external file. If the file
      // changes, the hash should change as well, but finding out what file to
      // hash is too hard for ccache, so just bail out.
      LOG_RAW(
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

  return Statistic::none;
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static optional<Digest>
result_key_from_depfile(Context& ctx, Hash& hash)
{
  std::string file_content;
  try {
    file_content = Util::read_file(ctx.args_info.output_dep);
  } catch (const Error& e) {
    LOG(
      "Cannot open dependency file {}: {}", ctx.args_info.output_dep, e.what());
    return nullopt;
  }

  for (string_view token : Depfile::tokenize(file_content)) {
    if (token.ends_with(":")) {
      continue;
    }
    if (!ctx.has_absolute_include_headers) {
      ctx.has_absolute_include_headers = util::is_absolute_path(token);
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

  if (ctx.diagnostics_color_failed) {
    DEBUG_ASSERT(ctx.config.compiler_type() == CompilerType::gcc);
    args.erase_last("-fdiagnostics-color");
  }
  int status = execute(ctx,
                       args.to_argv().data(),
                       std::move(tmp_stdout.fd),
                       std::move(tmp_stderr.fd));
  if (status != 0 && !ctx.diagnostics_color_failed
      && ctx.config.compiler_type() == CompilerType::gcc) {
    auto errors = Util::read_file(tmp_stderr.path);
    if (errors.find("fdiagnostics-color") != std::string::npos) {
      // GCC versions older than 4.9 don't understand -fdiagnostics-color, and
      // non-GCC compilers misclassified as CompilerType::gcc might not do it
      // either. We assume that if the error message contains
      // "fdiagnostics-color" then the compilation failed due to
      // -fdiagnostics-color being unsupported and we then retry without the
      // flag. (Note that there intentionally is no leading dash in
      // "fdiagnostics-color" since some compilers don't include the dash in the
      // error message.)
      LOG_RAW("-fdiagnostics-color is unsupported; trying again without it");

      tmp_stdout.fd = Fd(open(
        tmp_stdout.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stdout.fd) {
        LOG("Failed to truncate {}: {}", tmp_stdout.path, strerror(errno));
        throw Failure(Statistic::internal_error);
      }

      tmp_stderr.fd = Fd(open(
        tmp_stderr.path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
      if (!tmp_stderr.fd) {
        LOG("Failed to truncate {}: {}", tmp_stderr.path, strerror(errno));
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
update_manifest_file(Context& ctx,
                     const Digest& manifest_key,
                     const Digest& result_key)
{
  if (ctx.config.read_only() || ctx.config.read_only_direct()) {
    return;
  }

  MTR_BEGIN("manifest", "manifest_put");

  // See comment in get_file_hash_index for why saving of timestamps is forced
  // for precompiled headers.
  const bool save_timestamp =
    (ctx.config.sloppiness() & SLOPPY_FILE_STAT_MATCHES)
    || ctx.args_info.output_is_precompiled_header;

  ctx.storage.put(
    manifest_key, core::CacheEntryType::manifest, [&](const std::string& path) {
      LOG("Adding result key to {}", path);
      return Manifest::put(ctx.config,
                           path,
                           result_key,
                           ctx.included_files,
                           ctx.time_of_compilation,
                           save_timestamp);
    });

  MTR_END("manifest", "manifest_put");
}

struct FindCoverageFileResult
{
  bool found;
  std::string path;
  bool mangled;
};

static FindCoverageFileResult
find_coverage_file(const Context& ctx)
{
  // GCC 9+ writes coverage data for /dir/to/example.o to #dir#to#example.gcno
  // (in CWD) if -fprofile-dir=DIR is present (regardless of DIR) instead of the
  // traditional /dir/to/example.gcno.

  std::string mangled_form = Result::gcno_file_in_mangled_form(ctx);
  std::string unmangled_form = Result::gcno_file_in_unmangled_form(ctx);
  std::string found_file;
  if (Stat::stat(mangled_form)) {
    LOG("Found coverage file {}", mangled_form);
    found_file = mangled_form;
  }
  if (Stat::stat(unmangled_form)) {
    LOG("Found coverage file {}", unmangled_form);
    if (!found_file.empty()) {
      LOG_RAW("Found two coverage files, cannot continue");
      return {};
    }
    found_file = unmangled_form;
  }
  if (found_file.empty()) {
    LOG("No coverage file found (tried {} and {}), cannot continue",
        unmangled_form,
        mangled_form);
    return {};
  }
  return {true, found_file, found_file == mangled_form};
}

static void
write_result(Context& ctx,
             const std::string& result_path,
             const Stat& obj_stat,
             const std::string& stderr_path)
{
  Result::Writer result_writer(ctx, result_path);

  const auto stderr_stat = Stat::stat(stderr_path, Stat::OnError::log);
  if (!stderr_stat) {
    throw Failure(Statistic::internal_error);
  }

  if (stderr_stat.size() > 0) {
    result_writer.write(Result::FileType::stderr_output, stderr_path);
  }
  if (obj_stat) {
    result_writer.write(Result::FileType::object, ctx.args_info.output_obj);
  }
  if (ctx.args_info.generating_dependencies) {
    result_writer.write(Result::FileType::dependency, ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    const auto coverage_file = find_coverage_file(ctx);
    if (!coverage_file.found) {
      throw Failure(Statistic::internal_error);
    }
    result_writer.write(coverage_file.mangled
                          ? Result::FileType::coverage_mangled
                          : Result::FileType::coverage_unmangled,
                        coverage_file.path);
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

  const auto file_size_and_count_diff = result_writer.finalize();
  if (file_size_and_count_diff) {
    ctx.storage.primary().increment_statistic(
      Statistic::cache_size_kibibyte, file_size_and_count_diff->size_kibibyte);
    ctx.storage.primary().increment_statistic(Statistic::files_in_cache,
                                              file_size_and_count_diff->count);
  } else {
    LOG("Error: {}", file_size_and_count_diff.error());
    throw Failure(Statistic::internal_error);
  }
}

// Run the real compiler and put the result in cache. Returns the result key.
static Digest
to_cache(Context& ctx,
         Args& args,
         nonstd::optional<Digest> result_key,
         const Args& depend_extra_args,
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
    // nonexistent .dwo files.
    if (unlink(ctx.args_info.output_dwo.c_str()) != 0 && errno != ENOENT
        && errno != ESTALE) {
      LOG("Failed to unlink {}: {}", ctx.args_info.output_dwo, strerror(errno));
      throw Failure(Statistic::bad_output_file);
    }
  }

  LOG_RAW("Running real compiler");
  MTR_BEGIN("execute", "compiler");

  TemporaryFile tmp_stdout(FMT("{}/tmp.stdout", ctx.config.temporary_dir()));
  ctx.register_pending_tmp_file(tmp_stdout.path);
  std::string tmp_stdout_path = tmp_stdout.path;

  TemporaryFile tmp_stderr(FMT("{}/tmp.stderr", ctx.config.temporary_dir()));
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
  if (st.size() != 0 && ctx.config.compiler_type() != CompilerType::pump) {
    LOG_RAW("Compiler produced stdout");
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
    LOG("Compiler gave exit status {}", status);

    // We can output stderr immediately instead of rerunning the compiler.
    Util::send_to_stderr(ctx, Util::read_file(tmp_stderr_path));

    throw Failure(Statistic::compile_failed, status);
  }

  if (ctx.config.depend_mode()) {
    ASSERT(depend_mode_hash);
    result_key = result_key_from_depfile(ctx, *depend_mode_hash);
    if (!result_key) {
      throw Failure(Statistic::internal_error);
    }
  }

  ASSERT(result_key);

  bool produce_dep_file = ctx.args_info.generating_dependencies
                          && ctx.args_info.output_dep != "/dev/null";

  if (produce_dep_file) {
    Depfile::make_paths_relative_in_output_dep(ctx);
  }

  const auto obj_stat = Stat::stat(ctx.args_info.output_obj);
  if (!obj_stat) {
    if (ctx.args_info.expect_output_obj) {
      LOG_RAW("Compiler didn't produce an object file (unexpected)");
      throw Failure(Statistic::compiler_produced_no_output);
    } else {
      LOG_RAW("Compiler didn't produce an object file (expected)");
    }
  } else if (obj_stat.size() == 0) {
    LOG_RAW("Compiler produced an empty object file");
    throw Failure(Statistic::compiler_produced_empty_output);
  }

  MTR_BEGIN("result", "result_put");
  const bool added = ctx.storage.put(
    *result_key, core::CacheEntryType::result, [&](const std::string& path) {
      write_result(ctx, path, obj_stat, tmp_stderr_path);
      return true;
    });
  if (!added) {
    throw Failure(Statistic::internal_error);
  }
  MTR_END("result", "result_put");

  // Everything OK.
  Util::send_to_stderr(ctx, Util::read_file(tmp_stderr_path));

  return *result_key;
}

// Find the result key by running the compiler in preprocessor mode and
// hashing the result.
static Digest
get_result_key_from_cpp(Context& ctx, Args& args, Hash& hash)
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
      FMT("{}/tmp.cpp_stdout", ctx.config.temporary_dir()));
    ctx.register_pending_tmp_file(tmp_stdout.path);

    // stdout_path needs the proper cpp_extension for the compiler to do its
    // thing correctly.
    stdout_path = FMT("{}.{}", tmp_stdout.path, ctx.config.cpp_extension());
    Util::hard_link(tmp_stdout.path, stdout_path);
    ctx.register_pending_tmp_file(stdout_path);

    TemporaryFile tmp_stderr(
      FMT("{}/tmp.cpp_stderr", ctx.config.temporary_dir()));
    stderr_path = tmp_stderr.path;
    ctx.register_pending_tmp_file(stderr_path);

    size_t args_added = 2;
    args.push_back("-E");
    if (ctx.args_info.actual_language == "hip") {
      args.push_back("-o");
      args.push_back("-");
      args_added += 2;
    }
    if (ctx.config.keep_comments_cpp()) {
      args.push_back("-C");
      args_added++;
    }
    args.push_back(ctx.args_info.input_file);
    add_prefix(ctx, args, ctx.config.prefix_command_cpp());
    LOG_RAW("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    status =
      do_execute(ctx, args, std::move(tmp_stdout), std::move(tmp_stderr));
    MTR_END("execute", "preprocessor");
    args.pop_back(args_added);
  }

  if (status != 0) {
    LOG("Preprocessor gave exit status {}", status);
    throw Failure(Statistic::preprocessor_error);
  }

  hash.hash_delimiter("cpp");
  const bool is_pump = ctx.config.compiler_type() == CompilerType::pump;
  const Statistic error =
    process_preprocessed_file(ctx, hash, stdout_path, is_pump);
  if (error != Statistic::none) {
    throw Failure(error);
  }

  hash.hash_delimiter("cppstderr");
  if (!ctx.args_info.direct_i_file && !hash.hash_file(stderr_path)) {
    // Somebody removed the temporary file?
    LOG("Failed to open {}: {}", stderr_path, strerror(errno));
    throw Failure(Statistic::internal_error);
  }

  if (ctx.args_info.direct_i_file) {
    ctx.i_tmpfile = ctx.args_info.input_file;
  } else {
    ctx.i_tmpfile = stdout_path;
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
      LOG("Failure running compiler check command: {}",
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
        std::string path = FMT("{}/{}", ccbin, compiler);
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

  // Hash variables that may affect the compilation.
  const char* always_hash_env_vars[] = {
    // From <https://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html>:
    "COMPILER_PATH",
    "GCC_COMPARE_DEBUG",
    "GCC_EXEC_PREFIX",
    // Note: SOURCE_DATE_EPOCH is handled in hash_source_code_string().
  };
  for (const char* name : always_hash_env_vars) {
    const char* value = getenv(name);
    if (value) {
      hash.hash_delimiter(name);
      hash.hash(value);
    }
  }

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
        LOG("Relocating debuginfo from {} to {} (CWD: {})",
            old_path,
            new_path,
            ctx.apparent_cwd);
        if (Util::starts_with(ctx.apparent_cwd, old_path)) {
          dir_to_hash = new_path + ctx.apparent_cwd.substr(old_path.size());
        }
      }
    }
    LOG("Hashing CWD {}", dir_to_hash);
    hash.hash_delimiter("cwd");
    hash.hash(dir_to_hash);
  }

  if ((!should_rewrite_dependency_target(ctx.args_info)
       && ctx.args_info.generating_dependencies)
      || ctx.args_info.seen_split_dwarf || ctx.args_info.profile_arcs) {
    // If generating dependencies: The output object file name is part of the .d
    // file, so include the path in the hash.
    //
    // When using -gsplit-dwarf: Object files include a link to the
    // corresponding .dwo file based on the target object filename, so hashing
    // the object file path will do it, although just hashing the object file
    // base name would be enough.
    //
    // When using -fprofile-arcs (including implicitly via --coverage): the
    // object file contains a .gcda path based on the object file path.
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
    std::string gcda_path = FMT("{}/{}.gcda", dir, stem);
    LOG("Hashing coverage path {}", gcda_path);
    hash.hash_delimiter("gcda");
    hash.hash(gcda_path);
  }

  // Possibly hash the sanitize blacklist file path.
  for (const auto& sanitize_blacklist : args_info.sanitize_blacklists) {
    LOG("Hashing sanitize blacklist {}", sanitize_blacklist);
    hash.hash("sanitizeblacklist");
    if (!hash_binary_file(ctx, hash, sanitize_blacklist)) {
      throw Failure(Statistic::error_hashing_extra_file);
    }
  }

  if (!ctx.config.extra_files_to_hash().empty()) {
    for (const std::string& path :
         util::split_path_list(ctx.config.extra_files_to_hash())) {
      LOG("Hashing extra file {}", path);
      hash.hash_delimiter("extrafile");
      if (!hash_binary_file(ctx, hash, path)) {
        throw Failure(Statistic::error_hashing_extra_file);
      }
    }
  }

  // Possibly hash GCC_COLORS (for color diagnostics).
  if (ctx.config.compiler_type() == CompilerType::gcc) {
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
    FMT("{}/{}.gcda", profile_path, base_name),
    // -fprofile-use[=dir]/-fbranch-probabilities (GCC >=9)
    FMT("{}/{}#{}.gcda", profile_path, hashified_cwd, base_name),
    // -fprofile(-instr|-sample)-use=file (Clang), -fauto-profile=file (GCC >=5)
    profile_path,
    // -fprofile(-instr|-sample)-use=dir (Clang)
    FMT("{}/default.profdata", profile_path),
    // -fauto-profile (GCC >=5)
    "fbdata.afdo", // -fprofile-dir is not used
  };

  bool found = false;
  for (const std::string& p : paths_to_try) {
    LOG("Checking for profile data file {}", p);
    auto st = Stat::stat(p);
    if (st && !st.is_directory()) {
      LOG("Adding profile data {} to the hash", p);
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
// modes and calculate the result key. Returns the result key on success, and
// if direct_mode is true also the manifest key.
static std::pair<nonstd::optional<Digest>, nonstd::optional<Digest>>
calculate_result_and_manifest_key(Context& ctx,
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
  int is_clang = ctx.config.compiler_type() == CompilerType::clang
                 || ctx.config.compiler_type() == CompilerType::other;

  // First the arguments.
  for (size_t i = 1; i < args.size(); i++) {
    // Trust the user if they've said we should not hash a given option.
    if (option_should_be_ignored(args[i], ctx.ignore_options())) {
      LOG("Not hashing ignored option: {}", args[i]);
      if (i + 1 < args.size() && compopt_takes_arg(args[i])) {
        i++;
        LOG("Not hashing argument of ignored option: {}", args[i]);
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
      if (compopt_affects_cpp_output(args[i])) {
        if (compopt_takes_arg(args[i])) {
          i++;
        }
        continue;
      }
      if (compopt_affects_cpp_output(args[i].substr(0, 2))) {
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
        || Util::starts_with(args[i], "--specs=")
        || (args[i] == "-specs" || args[i] == "--specs")
        || args[i] == "--config") {
      std::string path;
      size_t eq_pos = args[i].find('=');
      if (eq_pos == std::string::npos) {
        if (i + 1 >= args.size()) {
          LOG("missing argument for \"{}\"", args[i]);
          throw Failure(Statistic::bad_compiler_arguments);
        }
        path = args[i + 1];
        i++;
      } else {
        path = args[i].substr(eq_pos + 1);
      }
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
    ASSERT(!ctx.args_info.profile_path.empty());

    // For a relative profile directory D the compiler stores $PWD/D as part of
    // the profile filename so we need to include the same information in the
    // hash.
    const std::string profile_path =
      util::is_absolute_path(ctx.args_info.profile_path)
        ? ctx.args_info.profile_path
        : FMT("{}/{}", ctx.apparent_cwd, ctx.args_info.profile_path);
    LOG("Adding profile directory {} to our hash", profile_path);
    hash.hash_delimiter("-fprofile-dir");
    hash.hash(profile_path);
  }

  if (ctx.args_info.profile_use && !hash_profile_data_file(ctx, hash)) {
    LOG_RAW("No profile data file found");
    throw Failure(Statistic::no_input_file);
  }

  // Adding -arch to hash since cpp output is affected.
  for (const auto& arch : ctx.args_info.arch_args) {
    hash.hash_delimiter("-arch");
    hash.hash(arch);
  }

  nonstd::optional<Digest> result_key;
  nonstd::optional<Digest> manifest_key;

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
      LOG_RAW("Disabling direct mode");
      ctx.config.set_direct_mode(false);
      return {nullopt, nullopt};
    }

    manifest_key = hash.digest();

    const auto manifest_path =
      ctx.storage.get(*manifest_key, core::CacheEntryType::manifest);

    if (manifest_path) {
      LOG("Looking for result key in {}", *manifest_path);
      MTR_BEGIN("manifest", "manifest_get");
      result_key = Manifest::get(ctx, *manifest_path);
      MTR_END("manifest", "manifest_get");
      if (result_key) {
        LOG_RAW("Got result key from manifest");
      } else {
        LOG_RAW("Did not find result key in manifest");
      }
    }
  } else {
    if (ctx.args_info.arch_args.empty()) {
      result_key = get_result_key_from_cpp(ctx, preprocessor_args, hash);
      LOG_RAW("Got result key from preprocessor");
    } else {
      preprocessor_args.push_back("-arch");
      for (size_t i = 0; i < ctx.args_info.arch_args.size(); ++i) {
        preprocessor_args.push_back(ctx.args_info.arch_args[i]);
        result_key = get_result_key_from_cpp(ctx, preprocessor_args, hash);
        LOG("Got result key from preprocessor with -arch {}",
            ctx.args_info.arch_args[i]);
        if (i != ctx.args_info.arch_args.size() - 1) {
          result_key = nullopt;
        }
        preprocessor_args.pop_back();
      }
      preprocessor_args.pop_back();
    }
  }

  return {result_key, manifest_key};
}

enum class FromCacheCallMode { direct, cpp };

// Try to return the compile result from cache.
static optional<Statistic>
from_cache(Context& ctx, FromCacheCallMode mode, const Digest& result_key)
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
  if ((ctx.config.compiler_type() == CompilerType::clang
       || ctx.config.compiler_type() == CompilerType::other)
      && ctx.args_info.output_is_precompiled_header
      && !ctx.args_info.fno_pch_timestamp && mode == FromCacheCallMode::cpp) {
    LOG_RAW("Not considering cached precompiled header in preprocessor mode");
    return nullopt;
  }

  MTR_BEGIN("cache", "from_cache");

  // Get result from cache.
  const auto result_path =
    ctx.storage.get(result_key, core::CacheEntryType::result);
  if (!result_path) {
    return nullopt;
  }

  Result::Reader result_reader(*result_path);
  ResultRetriever result_retriever(
    ctx, should_rewrite_dependency_target(ctx.args_info));

  auto error = result_reader.read(result_retriever);
  MTR_END("cache", "from_cache");
  if (error) {
    LOG("Failed to get result from cache: {}", *error);
    return nullopt;
  }

  LOG_RAW("Succeeded getting cached result");

  return mode == FromCacheCallMode::direct ? Statistic::direct_cache_hit
                                           : Statistic::preprocessed_cache_hit;
}

// Find the real compiler and put it into ctx.orig_args[0]. We just search the
// PATH to find an executable of the same name that isn't a link to ourselves.
// Pass find_executable function as second parameter.
void
find_compiler(Context& ctx,
              const FindExecutableFunction& find_executable_function)
{
  // gcc --> 0
  // ccache gcc --> 1
  // ccache ccache gcc --> 2
  size_t compiler_pos = 0;
  while (compiler_pos < ctx.orig_args.size()
         && Util::same_program_name(
           Util::base_name(ctx.orig_args[compiler_pos]), CCACHE_NAME)) {
    ++compiler_pos;
  }

  // Support user override of the compiler.
  const std::string compiler =
    !ctx.config.compiler().empty()
      ? ctx.config.compiler()
      // In case ccache is masquerading as the compiler, use only base_name so
      // the real compiler can be determined.
      : (compiler_pos == 0 ? std::string(Util::base_name(ctx.orig_args[0]))
                           : ctx.orig_args[compiler_pos]);

  const std::string resolved_compiler =
    util::is_full_path(compiler)
      ? compiler
      : find_executable_function(ctx, compiler, CCACHE_NAME);

  if (resolved_compiler.empty()) {
    throw Fatal("Could not find compiler \"{}\" in PATH", compiler);
  }

  if (Util::same_program_name(Util::base_name(resolved_compiler),
                              CCACHE_NAME)) {
    throw Fatal(
      "Recursive invocation (the name of the ccache binary must be \"{}\")",
      CCACHE_NAME);
  }

  ctx.orig_args.pop_front(compiler_pos);
  ctx.orig_args[0] = resolved_compiler;
}

// Initialize ccache. Must be called once before anything else is run.
static void
initialize(Context& ctx, int argc, const char* const* argv)
{
  ctx.orig_args = Args::from_argv(argc, argv);
  ctx.storage.initialize();

  LOG("=== CCACHE {} STARTED =========================================",
      CCACHE_VERSION);

  if (getenv("CCACHE_INTERNAL_TRACE")) {
#ifdef MTR_ENABLED
    ctx.mini_trace = std::make_unique<MiniTrace>(ctx.args_info);
#else
    LOG_RAW("Error: tracing is not enabled!");
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
    LOG("dup(2) failed: {}", strerror(errno));
    throw Failure(Statistic::internal_error);
  }

  Util::setenv("UNCACHED_ERR_FD", FMT("{}", uncached_fd));
}

static void
configuration_logger(const std::string& key,
                     const std::string& value,
                     const std::string& origin)
{
  BULK_LOG("Config: ({}) {} = {}", origin, key, value);
}

static void
configuration_printer(const std::string& key,
                      const std::string& value,
                      const std::string& origin)
{
  PRINT(stdout, "({}) {} = {}\n", origin, key, value);
}

static int cache_compilation(int argc, const char* const* argv);
static Statistic do_cache_compilation(Context& ctx, const char* const* argv);

static void
finalize_at_exit(Context& ctx)
{
  try {
    if (ctx.config.disable()) {
      // Just log result, don't update statistics.
      LOG_RAW("Result: disabled");
      return;
    }

    if (!ctx.config.log_file().empty() || ctx.config.debug()) {
      const auto result = ctx.storage.primary().get_result_message();
      if (result) {
        LOG("Result: {}", *result);
      }
    }

    if (!ctx.config.stats_log().empty()) {
      const auto result_id = ctx.storage.primary().get_result_id();
      if (result_id) {
        Statistics::log_result(
          ctx.config.stats_log(), ctx.args_info.input_file, *result_id);
      }
    }

    ctx.storage.finalize();
  } catch (const ErrorBase& e) {
    // finalize_at_exit must not throw since it's called by a destructor.
    LOG("Error while finalizing stats: {}", e.what());
  }

  // Dump log buffer last to not lose any logs.
  if (ctx.config.debug() && !ctx.args_info.output_obj.empty()) {
    Logging::dump_log(prepare_debug_path(
      ctx.config.debug_dir(), ctx.args_info.output_obj, "log"));
  }
}

// The entry point when invoked to cache a compilation.
static int
cache_compilation(int argc, const char* const* argv)
{
  tzset(); // Needed for localtime_r.

  bool fall_back_to_original_compiler = false;
  Args saved_orig_args;
  nonstd::optional<uint32_t> original_umask;
  std::string saved_temp_dir;

  {
    Context ctx;
    SignalHandler signal_handler(ctx);
    Finalizer finalizer([&ctx] { finalize_at_exit(ctx); });

    initialize(ctx, argc, argv);

    MTR_BEGIN("main", "find_compiler");
    find_compiler(ctx, &find_executable);
    MTR_END("main", "find_compiler");

    try {
      Statistic statistic = do_cache_compilation(ctx, argv);
      ctx.storage.primary().increment_statistic(statistic);
    } catch (const Failure& e) {
      if (e.statistic() != Statistic::none) {
        ctx.storage.primary().increment_statistic(e.statistic());
      }

      if (e.exit_code()) {
        return *e.exit_code();
      }
      // Else: Fall back to running the real compiler.
      fall_back_to_original_compiler = true;

      original_umask = ctx.original_umask;

      ASSERT(!ctx.orig_args.empty());

      ctx.orig_args.erase_with_prefix("--ccache-");
      add_prefix(ctx, ctx.orig_args, ctx.config.prefix_command());

      LOG_RAW("Failed; falling back to running the real compiler");

      saved_temp_dir = ctx.config.temporary_dir();
      saved_orig_args = std::move(ctx.orig_args);
      auto execv_argv = saved_orig_args.to_argv();
      LOG("Executing {}", Util::format_argv_for_logging(execv_argv.data()));
      // Execute the original command below after ctx and finalizer have been
      // destructed.
    }
  }

  if (fall_back_to_original_compiler) {
    if (original_umask) {
      umask(*original_umask);
    }
    auto execv_argv = saved_orig_args.to_argv();
    execute_noreturn(execv_argv.data(), saved_temp_dir);
    throw Fatal(
      "execute_noreturn of {} failed: {}", execv_argv[0], strerror(errno));
  }

  return EXIT_SUCCESS;
}

static Statistic
do_cache_compilation(Context& ctx, const char* const* argv)
{
  if (ctx.actual_cwd.empty()) {
    LOG("Unable to determine current working directory: {}", strerror(errno));
    throw Failure(Statistic::internal_error);
  }

  if (!ctx.config.log_file().empty() || ctx.config.debug()) {
    ctx.config.visit_items(configuration_logger);
  }

  // Guess compiler after logging the config value in order to be able to
  // display "compiler_type = auto" before overwriting the value with the
  // guess.
  if (ctx.config.compiler_type() == CompilerType::auto_guess) {
    ctx.config.set_compiler_type(guess_compiler(ctx.orig_args[0]));
  }
  DEBUG_ASSERT(ctx.config.compiler_type() != CompilerType::auto_guess);

  if (ctx.config.disable()) {
    LOG_RAW("ccache is disabled");
    // Statistic::cache_miss is a dummy to trigger stats_flush.
    throw Failure(Statistic::cache_miss);
  }

  LOG("Command line: {}", Util::format_argv_for_logging(argv));
  LOG("Hostname: {}", Util::get_hostname());
  LOG("Working directory: {}", ctx.actual_cwd);
  if (ctx.apparent_cwd != ctx.actual_cwd) {
    LOG("Apparent working directory: {}", ctx.apparent_cwd);
  }

  LOG("Compiler type: {}", compiler_type_to_string(ctx.config.compiler_type()));

  MTR_BEGIN("main", "process_args");
  ProcessArgsResult processed = process_args(ctx);
  MTR_END("main", "process_args");

  if (processed.error) {
    throw Failure(*processed.error);
  }

  set_up_uncached_err();

  if (ctx.config.depend_mode()
      && (!ctx.args_info.generating_dependencies
          || ctx.args_info.output_dep == "/dev/null"
          || !ctx.config.run_second_cpp())) {
    LOG_RAW("Disabling depend mode");
    ctx.config.set_depend_mode(false);
  }

  LOG("Source file: {}", ctx.args_info.input_file);
  if (ctx.args_info.generating_dependencies) {
    LOG("Dependency file: {}", ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    LOG_RAW("Coverage file is being generated");
  }
  if (ctx.args_info.generating_stackusage) {
    LOG("Stack usage file: {}", ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    LOG("Diagnostics file: {}", ctx.args_info.output_dia);
  }
  if (!ctx.args_info.output_dwo.empty()) {
    LOG("Split dwarf file: {}", ctx.args_info.output_dwo);
  }

  LOG("Object file: {}", ctx.args_info.output_obj);
  MTR_META_THREAD_NAME(ctx.args_info.output_obj.c_str());

  if (ctx.config.debug()) {
    const auto path = prepare_debug_path(
      ctx.config.debug_dir(), ctx.args_info.output_obj, "input-text");
    File debug_text_file(path, "w");
    if (debug_text_file) {
      ctx.hash_debug_files.push_back(std::move(debug_text_file));
    } else {
      LOG("Failed to open {}: {}", path, strerror(errno));
    }
  }

  FILE* debug_text_file = !ctx.hash_debug_files.empty()
                            ? ctx.hash_debug_files.front().get()
                            : nullptr;

  Hash common_hash;
  init_hash_debug(ctx, common_hash, 'c', "COMMON", debug_text_file);

  MTR_BEGIN("hash", "common_hash");
  hash_common_info(
    ctx, processed.preprocessor_args, common_hash, ctx.args_info);
  MTR_END("hash", "common_hash");

  // Try to find the hash using the manifest.
  Hash direct_hash = common_hash;
  init_hash_debug(ctx, direct_hash, 'd', "DIRECT MODE", debug_text_file);

  Args args_to_hash = processed.preprocessor_args;
  args_to_hash.push_back(processed.extra_args_to_hash);

  bool put_result_in_manifest = false;
  optional<Digest> result_key;
  optional<Digest> result_key_from_manifest;
  optional<Digest> manifest_key;

  if (ctx.config.direct_mode()) {
    LOG_RAW("Trying direct lookup");
    MTR_BEGIN("hash", "direct_hash");
    Args dummy_args;
    std::tie(result_key, manifest_key) = calculate_result_and_manifest_key(
      ctx, args_to_hash, dummy_args, direct_hash, true);
    MTR_END("hash", "direct_hash");
    if (result_key) {
      // If we can return from cache at this point then do so.
      auto result = from_cache(ctx, FromCacheCallMode::direct, *result_key);
      if (result) {
        return *result;
      }

      // Wasn't able to return from cache at this point. However, the result
      // was already found in manifest, so don't re-add it later.
      put_result_in_manifest = false;

      result_key_from_manifest = result_key;
    } else {
      // Add result to manifest later.
      put_result_in_manifest = true;
    }
  }

  if (ctx.config.read_only_direct()) {
    LOG_RAW("Read-only direct mode; running real compiler");
    throw Failure(Statistic::cache_miss);
  }

  if (!ctx.config.depend_mode()) {
    // Find the hash using the preprocessed output. Also updates
    // ctx.included_files.
    Hash cpp_hash = common_hash;
    init_hash_debug(ctx, cpp_hash, 'p', "PREPROCESSOR MODE", debug_text_file);

    MTR_BEGIN("hash", "cpp_hash");
    result_key =
      calculate_result_and_manifest_key(
        ctx, args_to_hash, processed.preprocessor_args, cpp_hash, false)
        .first;
    MTR_END("hash", "cpp_hash");

    // calculate_result_and_manifest_key always returns a non-nullopt result_key
    // if the last argument (direct_mode) is false.
    ASSERT(result_key);

    if (result_key_from_manifest && result_key_from_manifest != result_key) {
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
      LOG_RAW("Hash from manifest doesn't match preprocessor output");
      LOG_RAW("Likely reason: different CCACHE_BASEDIRs used");
      LOG_RAW("Removing manifest as a safety measure");
      ctx.storage.remove(*result_key, core::CacheEntryType::result);

      put_result_in_manifest = true;
    }

    // If we can return from cache at this point then do.
    const auto result = from_cache(ctx, FromCacheCallMode::cpp, *result_key);
    if (result) {
      if (manifest_key && put_result_in_manifest) {
        update_manifest_file(ctx, *manifest_key, *result_key);
      }
      return *result;
    }
  }

  if (ctx.config.read_only()) {
    LOG_RAW("Read-only mode; running real compiler");
    throw Failure(Statistic::cache_miss);
  }

  add_prefix(ctx, processed.compiler_args, ctx.config.prefix_command());

  // In depend_mode, extend the direct hash.
  Hash* depend_mode_hash = ctx.config.depend_mode() ? &direct_hash : nullptr;

  // Run real compiler, sending output to cache.
  MTR_BEGIN("cache", "to_cache");
  result_key = to_cache(ctx,
                        processed.compiler_args,
                        result_key,
                        ctx.args_info.depend_extra_args,
                        depend_mode_hash);
  if (ctx.config.direct_mode()) {
    ASSERT(manifest_key);
    update_manifest_file(ctx, *manifest_key, *result_key);
  }
  MTR_END("cache", "to_cache");

  return Statistic::cache_miss;
}

// The main program when not doing a compile.
static int
handle_main_options(int argc, const char* const* argv)
{
  enum longopts {
    CHECKSUM_FILE,
    CONFIG_PATH,
    DUMP_MANIFEST,
    DUMP_RESULT,
    EVICT_OLDER_THAN,
    EXTRACT_RESULT,
    HASH_FILE,
    PRINT_STATS,
    SHOW_LOG_STATS,
  };
  static const struct option options[] = {
    {"checksum-file", required_argument, nullptr, CHECKSUM_FILE},
    {"cleanup", no_argument, nullptr, 'c'},
    {"clear", no_argument, nullptr, 'C'},
    {"config-path", required_argument, nullptr, CONFIG_PATH},
    {"directory", required_argument, nullptr, 'd'},
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
    {"show-log-stats", no_argument, nullptr, SHOW_LOG_STATS},
    {"show-stats", no_argument, nullptr, 's'},
    {"version", no_argument, nullptr, 'V'},
    {"zero-stats", no_argument, nullptr, 'z'},
    {nullptr, 0, nullptr, 0}};

  int c;
  while ((c = getopt_long(argc,
                          const_cast<char* const*>(argv),
                          "cCd:k:hF:M:po:sVxX:z",
                          options,
                          nullptr))
         != -1) {
    Context ctx;

    std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case CHECKSUM_FILE: {
      Checksum checksum;
      Fd fd(arg == "-" ? STDIN_FILENO : open(arg.c_str(), O_RDONLY));
      Util::read_fd(*fd, [&checksum](const void* data, size_t size) {
        checksum.update(data, size);
      });
      PRINT(stdout, "{:016x}\n", checksum.digest());
      break;
    }

    case CONFIG_PATH:
      Util::setenv("CCACHE_CONFIGPATH", arg);
      break;

    case DUMP_MANIFEST:
      return Manifest::dump(arg, stdout) ? 0 : 1;

    case DUMP_RESULT: {
      ResultDumper result_dumper(stdout);
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_dumper);
      if (error) {
        PRINT(stderr, "Error: {}\n", *error);
      }
      return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    case EVICT_OLDER_THAN: {
      auto seconds = Util::parse_duration(arg);
      ProgressBar progress_bar("Evicting...");
      clean_old(
        ctx, [&](double progress) { progress_bar.update(progress); }, seconds);
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
      break;
    }

    case EXTRACT_RESULT: {
      ResultExtractor result_extractor(".");
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_extractor);
      if (error) {
        PRINT(stderr, "Error: {}\n", *error);
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
      PRINT(stdout, "{}\n", hash.digest().to_string());
      break;
    }

    case PRINT_STATS: {
      Counters counters;
      time_t last_updated;
      std::tie(counters, last_updated) =
        Statistics::collect_counters(ctx.config);
      PRINT_RAW(stdout,
                Statistics::format_machine_readable(counters, last_updated));
      break;
    }

    case 'c': // --cleanup
    {
      ProgressBar progress_bar("Cleaning...");
      clean_up_all(ctx.config,
                   [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
      break;
    }

    case 'C': // --clear
    {
      ProgressBar progress_bar("Clearing...");
      wipe_all(ctx, [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
      break;
    }

    case 'd': // --directory
      Util::setenv("CCACHE_DIR", arg);
      break;

    case 'h': // --help
      PRINT(stdout, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_SUCCESS);

    case 'k': // --get-config
      PRINT(stdout, "{}\n", ctx.config.get_string_value(arg));
      break;

    case 'F': { // --max-files
      auto files = Util::parse_unsigned(arg);
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_files", arg);
      if (files == 0) {
        PRINT_RAW(stdout, "Unset cache file limit\n");
      } else {
        PRINT(stdout, "Set cache file limit to {}\n", files);
      }
      break;
    }

    case 'M': { // --max-size
      uint64_t size = Util::parse_size(arg);
      Config::set_value_in_file(
        ctx.config.primary_config_path(), "max_size", arg);
      if (size == 0) {
        PRINT_RAW(stdout, "Unset cache size limit\n");
      } else {
        PRINT(stdout,
              "Set cache size limit to {}\n",
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

    case SHOW_LOG_STATS: {
      if (ctx.config.stats_log().empty()) {
        throw Fatal("No stats log has been configured");
      }
      PRINT_RAW(stdout, Statistics::format_stats_log(ctx.config));
      Counters counters = Statistics::read_log(ctx.config.stats_log());
      auto st = Stat::stat(ctx.config.stats_log(), Stat::OnError::log);
      PRINT_RAW(stdout,
                Statistics::format_human_readable(counters, st.mtime(), true));
      break;
    }

    case 's': { // --show-stats
      PRINT_RAW(stdout, Statistics::format_config_header(ctx.config));
      Counters counters;
      time_t last_updated;
      std::tie(counters, last_updated) =
        Statistics::collect_counters(ctx.config);
      PRINT_RAW(
        stdout,
        Statistics::format_human_readable(counters, last_updated, false));
      PRINT_RAW(stdout, Statistics::format_config_footer(ctx.config));
      break;
    }

    case 'V': // --version
      PRINT(VERSION_TEXT, CCACHE_NAME, CCACHE_VERSION, FEATURE_TEXT);
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
      Statistics::zero_all_counters(ctx.config);
      PRINT_RAW(stdout, "Statistics zeroed\n");
      break;

    default:
      PRINT(stderr, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_FAILURE);
    }
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
        PRINT(stderr, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
        exit(EXIT_FAILURE);
      }
      // If the first argument isn't an option, then assume we are being
      // passed a compiler name and options.
      if (argv[1][0] == '-') {
        return handle_main_options(argc, argv);
      }
    }

    return cache_compilation(argc, argv);
  } catch (const ErrorBase& e) {
    PRINT(stderr, "ccache: error: {}\n", e.what());
    return EXIT_FAILURE;
  }
}
