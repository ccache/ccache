// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2022 Joel Rosdahl and other contributors
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
#include "Depfile.hpp"
#include "Fd.hpp"
#include "File.hpp"
#include "Finalizer.hpp"
#include "Hash.hpp"
#include "Logging.hpp"
#include "MiniTrace.hpp"
#include "Result.hpp"
#include "ResultRetriever.hpp"
#include "SignalHandler.hpp"
#include "TemporaryFile.hpp"
#include "UmaskScope.hpp"
#include "Util.hpp"
#include "Win32Util.hpp"
#include "argprocessing.hpp"
#include "compopt.hpp"
#include "execute.hpp"
#include "fmtmacros.hpp"
#include "hashutil.hpp"
#include "language.hpp"

#include <AtomicFile.hpp>
#include <compression/types.hpp>
#include <core/CacheEntryReader.hpp>
#include <core/CacheEntryWriter.hpp>
#include <core/FileReader.hpp>
#include <core/FileWriter.hpp>
#include <core/Manifest.hpp>
#include <core/Statistics.hpp>
#include <core/StatsLog.hpp>
#include <core/exceptions.hpp>
#include <core/mainoptions.hpp>
#include <core/types.hpp>
#include <core/wincompat.hpp>
#include <storage/Storage.hpp>
#include <util/expected.hpp>
#include <util/path.hpp>
#include <util/string.hpp>

#include "third_party/fmt/core.h"

#include <fcntl.h>

#include <optional>
#include <string_view>

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using core::Statistic;

// This is a string that identifies the current "version" of the hash sum
// computed by ccache. If, for any reason, we want to force the hash sum to be
// different for the same input in a new ccache version, we can just change
// this string. A typical example would be if the format of one of the files
// stored in the cache changes in a backwards-incompatible way.
const char HASH_PREFIX[] = "4";

namespace {

// Return nonstd::make_unexpected<Failure> if ccache did not succeed in getting
// or putting a result in the cache. If `exit_code` is set, ccache will just
// exit with that code directly, otherwise execute the real compiler and exit
// with its exit code. Statistics counters will also be incremented.
class Failure
{
public:
  Failure(Statistic statistic);
  Failure(std::initializer_list<Statistic> statistics);

  const core::StatisticsCounters& counters() const;
  std::optional<int> exit_code() const;
  void set_exit_code(int exit_code);

private:
  core::StatisticsCounters m_counters;
  std::optional<int> m_exit_code;
};

inline Failure::Failure(const Statistic statistic) : m_counters({statistic})
{
}

inline Failure::Failure(const std::initializer_list<Statistic> statistics)
  : m_counters(statistics)
{
}

inline const core::StatisticsCounters&
Failure::counters() const
{
  return m_counters;
}

inline std::optional<int>
Failure::exit_code() const
{
  return m_exit_code;
}

inline void
Failure::set_exit_code(const int exit_code)
{
  m_exit_code = exit_code;
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
    std::string path = find_executable(ctx, word, ctx.orig_args[0]);
    if (path.empty()) {
      throw core::Fatal("{}: {}", word, strerror(errno));
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
                   const timeval& time_of_invocation,
                   const std::string& output_obj,
                   std::string_view suffix)
{
  auto prefix = debug_dir.empty()
                  ? output_obj
                  : debug_dir + util::to_absolute_path_no_drive(output_obj);
  if (!Util::create_dir(Util::dir_name(prefix))) {
    // Ignore since we can't handle an error in another way in this context. The
    // caller takes care of logging when trying to open the path for writing.
  }

  char timestamp[100];
  const auto tm = Util::localtime(time_of_invocation.tv_sec);
  if (tm) {
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &*tm);
  } else {
    snprintf(timestamp,
             sizeof(timestamp),
             "%llu",
             static_cast<long long unsigned int>(time_of_invocation.tv_sec));
  }
  return FMT("{}.{}_{:06}.ccache-{}",
             prefix,
             timestamp,
             time_of_invocation.tv_usec,
             suffix);
}

static void
init_hash_debug(Context& ctx,
                Hash& hash,
                char type,
                std::string_view section_name,
                FILE* debug_text_file)
{
  if (!ctx.config.debug()) {
    return;
  }

  const auto path = prepare_debug_path(ctx.config.debug_dir(),
                                       ctx.time_of_invocation,
                                       ctx.args_info.output_obj,
                                       FMT("input-{}", type));
  File debug_binary_file(path, "wb");
  if (debug_binary_file) {
    hash.enable_debug(section_name, debug_binary_file.get(), debug_text_file);
    ctx.hash_debug_files.push_back(std::move(debug_binary_file));
  } else {
    LOG("Failed to open {}: {}", path, strerror(errno));
  }
}

CompilerType
guess_compiler(std::string_view path)
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

  const auto name =
    Util::to_lowercase(Util::remove_extension(Util::base_name(compiler_path)));
  if (name.find("clang-cl") != std::string_view::npos) {
    return CompilerType::clang_cl;
  } else if (name.find("clang") != std::string_view::npos) {
    return CompilerType::clang;
  } else if (name.find("gcc") != std::string_view::npos
             || name.find("g++") != std::string_view::npos) {
    return CompilerType::gcc;
  } else if (name.find("nvcc") != std::string_view::npos) {
    return CompilerType::nvcc;
  } else if (name == "icl") {
    return CompilerType::icl;
  } else if (name == "cl") {
    return CompilerType::msvc;
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
  if (!(ctx.config.sloppiness().is_enabled(core::Sloppy::include_file_mtime))
      && path_stat.mtime() >= ctx.time_of_compilation) {
    LOG("Include file {} too new", path);
    return true;
  }

  // The same >= logic as above applies to the change time of the file.
  if (!(ctx.config.sloppiness().is_enabled(core::Sloppy::include_file_ctime))
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

  if (path == ctx.args_info.normalized_input_file) {
    // Don't remember the input file.
    return true;
  }

  if (system
      && (ctx.config.sloppiness().is_enabled(core::Sloppy::system_headers))) {
    // Don't remember this system header.
    return true;
  }

  // Canonicalize path for comparison; Clang uses ./header.h.
  if (util::starts_with(path, "./")) {
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
  Digest file_digest;

  if (is_pch) {
    if (ctx.args_info.included_pch_file.empty()) {
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

    if (!hash_binary_file(ctx, file_digest, path)) {
      return false;
    }
    cpp_hash.hash_delimiter(using_pch_sum ? "pch_sum_hash" : "pch_hash");
    cpp_hash.hash(file_digest.to_string());
  }

  if (ctx.config.direct_mode()) {
    if (!is_pch) { // else: the file has already been hashed.
      int result = hash_source_code_file(ctx, file_digest, path);
      if (result & HASH_SOURCE_CODE_ERROR
          || result & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }
    }

    ctx.included_files.emplace(path, file_digest);

    if (depend_mode_hash) {
      depend_mode_hash->hash_delimiter("include");
      depend_mode_hash->hash(file_digest.to_string());
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
  for (const auto& [path, digest] : ctx.included_files) {
    PRINT(fp, "{}\n", path);
  }
}

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in ctx.included_files.
static nonstd::expected<void, Failure>
process_preprocessed_file(Context& ctx, Hash& hash, const std::string& path)
{
  std::string data;
  try {
    data = Util::read_file(path);
  } catch (core::Error&) {
    return nonstd::make_unexpected(Statistic::internal_error);
  }

  // Bytes between p and q are pending to be hashed.
  const char* p = &data[0];
  char* q = &data[0];
  const char* end = p + data.length();

  // There must be at least 7 characters (# 1 "x") left to potentially find an
  // include file path.
  while (q < end - 7) {
    static const std::string_view pragma_gcc_pch_preprocess =
      "pragma GCC pch_preprocess ";
    static const std::string_view hash_31_command_line_newline =
      "# 31 \"<command-line>\"\n";
    static const std::string_view hash_32_command_line_2_newline =
      "# 32 \"<command-line>\" 2\n";
    // Note: Intentionally not using the string form to avoid false positive
    // match by ccache itself.
    static const char incbin_directive[] = {'.', 'i', 'n', 'c', 'b', 'i', 'n'};

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
            || util::starts_with(&q[1], pragma_gcc_pch_preprocess)
            // HP/AIX:
            || (q[1] == 'l' && q[2] == 'i' && q[3] == 'n' && q[4] == 'e'
                && q[5] == ' '))
        && (q == data.data() || q[-1] == '\n')) {
      // Workarounds for preprocessor linemarker bugs in GCC version 6.
      if (q[2] == '3') {
        if (util::starts_with(q, hash_31_command_line_newline)) {
          // Bogus extra line with #31, after the regular #1: Ignore the whole
          // line, and continue parsing.
          hash.hash(p, q - p);
          while (q < end && *q != '\n') {
            q++;
          }
          q++;
          p = q;
          continue;
        } else if (util::starts_with(q, hash_32_command_line_2_newline)) {
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
        return nonstd::make_unexpected(Statistic::internal_error);
      }
      // q points to the beginning of an include file path
      hash.hash(p, q - p);
      p = q;
      while (q < end && *q != '"') {
        q++;
      }
      if (p == q) {
        // Skip empty file name.
        continue;
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
      inc_path = Util::normalize_concrete_absolute_path(inc_path);
      inc_path = Util::make_relative_path(ctx, inc_path);

      if ((inc_path != ctx.apparent_cwd) || ctx.config.hash_dir()) {
        hash.hash(inc_path);
      }

      if (remember_include_file(ctx, inc_path, hash, system, nullptr)
          == RememberIncludeFileResult::cannot_use_pch) {
        return nonstd::make_unexpected(
          Statistic::could_not_use_precompiled_header);
      }
      p = q; // Everything of interest between p and q has been hashed now.
    } else if (strncmp(q, incbin_directive, sizeof(incbin_directive)) == 0
               && ((q[7] == ' '
                    && (q[8] == '"' || (q[8] == '\\' && q[9] == '"')))
                   || q[7] == '"')) {
      // An assembler .inc bin (without the space) statement, which could be
      // part of inline assembly, refers to an external file. If the file
      // changes, the hash should change as well, but finding out what file to
      // hash is too hard for ccache, so just bail out.
      LOG_RAW(
        "Found potential unsupported .inc"
        "bin directive in source code");
      return nonstd::make_unexpected(
        Failure(Statistic::unsupported_code_directive));
    } else if (strncmp(q, "___________", 10) == 0
               && (q == data.data() || q[-1] == '\n')) {
      // Unfortunately the distcc-pump wrapper outputs standard output lines:
      // __________Using distcc-pump from /usr/bin
      // __________Using # distcc servers in pump mode
      // __________Shutting down distcc-pump include server
      hash.hash(p, q - p);
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
  if (!ctx.args_info.included_pch_file.empty()) {
    std::string pch_path =
      Util::make_relative_path(ctx, ctx.args_info.included_pch_file);
    hash.hash(pch_path);
    remember_include_file(ctx, pch_path, hash, false, nullptr);
  }

  bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
  if (debug_included) {
    print_included_files(ctx, stdout);
  }

  return {};
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static std::optional<Digest>
result_key_from_depfile(Context& ctx, Hash& hash)
{
  std::string file_content;
  try {
    file_content = Util::read_file(ctx.args_info.output_dep);
  } catch (const core::Error& e) {
    LOG(
      "Cannot open dependency file {}: {}", ctx.args_info.output_dep, e.what());
    return std::nullopt;
  }

  for (std::string_view token : Depfile::tokenize(file_content)) {
    if (util::ends_with(token, ":")) {
      continue;
    }
    std::string path = Util::make_relative_path(ctx, token);
    remember_include_file(ctx, path, hash, false, &hash);
  }

  // Explicitly check the .gch/.pch/.pth file as it may not be mentioned in the
  // dependencies output.
  if (!ctx.args_info.included_pch_file.empty()) {
    std::string pch_path =
      Util::make_relative_path(ctx, ctx.args_info.included_pch_file);
    hash.hash(pch_path);
    remember_include_file(ctx, pch_path, hash, false, nullptr);
  }

  bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
  if (debug_included) {
    print_included_files(ctx, stdout);
  }

  return hash.digest();
}

struct GetTmpFdResult
{
  Fd fd;
  std::string path;
};

static GetTmpFdResult
get_tmp_fd(Context& ctx,
           const std::string_view description,
           const bool capture_output)
{
  if (capture_output) {
    TemporaryFile tmp_stdout(
      FMT("{}/tmp.{}", ctx.config.temporary_dir(), description));
    ctx.register_pending_tmp_file(tmp_stdout.path);
    return {std::move(tmp_stdout.fd), std::move(tmp_stdout.path)};
  } else {
    const auto dev_null_path = util::get_dev_null_path();
    return {Fd(open(dev_null_path, O_WRONLY | O_BINARY)), dev_null_path};
  }
}

struct DoExecuteResult
{
  int exit_status;
  std::string stdout_data;
  std::string stderr_data;
};

// Execute the compiler/preprocessor, with logic to retry without requesting
// colored diagnostics messages if that fails.
static nonstd::expected<DoExecuteResult, Failure>
do_execute(Context& ctx, Args& args, const bool capture_stdout = true)
{
  UmaskScope umask_scope(ctx.original_umask);

  if (ctx.diagnostics_color_failed) {
    DEBUG_ASSERT(ctx.config.compiler_type() == CompilerType::gcc);
    args.erase_last("-fdiagnostics-color");
  }

  auto tmp_stdout = get_tmp_fd(ctx, "stdout", capture_stdout);
  auto tmp_stderr = get_tmp_fd(ctx, "stderr", true);

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

      ctx.diagnostics_color_failed = true;
      return do_execute(ctx, args, capture_stdout);
    }
  }

  try {
    return DoExecuteResult{
      status,
      capture_stdout ? Util::read_file(tmp_stdout.path) : std::string(),
      Util::read_file(tmp_stderr.path),
    };
  } catch (core::Error&) {
    // The stdout or stderr file was removed - cleanup in progress? Better bail
    // out.
    return nonstd::make_unexpected(Statistic::missing_cache_file);
  }
}

static core::Manifest
read_manifest(const std::string& path)
{
  core::Manifest manifest;
  File file(path, "rb");
  if (file) {
    try {
      core::FileReader file_reader(*file);
      core::CacheEntryReader reader(file_reader);
      manifest.read(reader);
      reader.finalize();
    } catch (const core::Error& e) {
      LOG("Error reading {}: {}", path, e.what());
    }
  }
  return manifest;
}

static void
save_manifest(const Config& config,
              const core::Manifest& manifest,
              const std::string& path)
{
  AtomicFile atomic_manifest_file(path, AtomicFile::Mode::binary);
  core::FileWriter file_writer(atomic_manifest_file.stream());
  core::CacheEntryHeader header(core::CacheEntryType::manifest,
                                compression::type_from_config(config),
                                compression::level_from_config(config),
                                time(nullptr),
                                CCACHE_VERSION,
                                config.namespace_());
  header.set_entry_size_from_payload_size(manifest.serialized_size());

  core::CacheEntryWriter writer(file_writer, header);
  manifest.write(writer);
  writer.finalize();
  atomic_manifest_file.commit();
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

  ASSERT(ctx.config.direct_mode());

  MTR_SCOPE("manifest", "manifest_put");

  // See comment in core::Manifest::get_file_info_index for why saving of
  // timestamps is forced for precompiled headers.
  const bool save_timestamp =
    (ctx.config.sloppiness().is_enabled(core::Sloppy::file_stat_matches))
    || ctx.args_info.output_is_precompiled_header;

  ctx.storage.put(
    manifest_key, core::CacheEntryType::manifest, [&](const auto& path) {
      LOG("Adding result key to {}", path);
      try {
        auto manifest = read_manifest(path);
        const bool added = manifest.add_result(result_key,
                                               ctx.included_files,
                                               ctx.time_of_compilation,
                                               save_timestamp);
        if (added) {
          save_manifest(ctx.config, manifest, path);
        }
        return added;
      } catch (const core::Error& e) {
        LOG("Failed to add result key to {}: {}", path, e.what());
        return false;
      }
    });
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

static bool
write_result(Context& ctx,
             const std::string& result_path,
             const Stat& obj_stat,
             const std::string& stdout_data,
             const std::string& stderr_data)
{
  Result::Writer result_writer(ctx, result_path);

  if (!stderr_data.empty()) {
    result_writer.write_data(Result::FileType::stderr_output, stderr_data);
  }
  // Write stdout only after stderr (better with MSVC), as ResultRetriever
  // will later print process them in the order they are read.
  if (!stdout_data.empty()) {
    result_writer.write_data(Result::FileType::stdout_output, stdout_data);
  }
  if (obj_stat) {
    result_writer.write_file(Result::FileType::object,
                             ctx.args_info.output_obj);
  }
  if (ctx.args_info.generating_dependencies) {
    result_writer.write_file(Result::FileType::dependency,
                             ctx.args_info.output_dep);
  }
  if (ctx.args_info.generating_coverage) {
    const auto coverage_file = find_coverage_file(ctx);
    if (!coverage_file.found) {
      return false;
    }
    result_writer.write_file(coverage_file.mangled
                               ? Result::FileType::coverage_mangled
                               : Result::FileType::coverage_unmangled,
                             coverage_file.path);
  }
  if (ctx.args_info.generating_stackusage) {
    result_writer.write_file(Result::FileType::stackusage,
                             ctx.args_info.output_su);
  }
  if (ctx.args_info.generating_diagnostics) {
    result_writer.write_file(Result::FileType::diagnostic,
                             ctx.args_info.output_dia);
  }
  if (ctx.args_info.seen_split_dwarf && Stat::stat(ctx.args_info.output_dwo)) {
    // Only store .dwo file if it was created by the compiler (GCC and Clang
    // behave differently e.g. for "-gsplit-dwarf -g1").
    result_writer.write_file(Result::FileType::dwarf_object,
                             ctx.args_info.output_dwo);
  }

  const auto file_size_and_count_diff = result_writer.finalize();
  if (file_size_and_count_diff) {
    ctx.storage.primary.increment_statistic(
      Statistic::cache_size_kibibyte, file_size_and_count_diff->size_kibibyte);
    ctx.storage.primary.increment_statistic(Statistic::files_in_cache,
                                            file_size_and_count_diff->count);
  } else {
    LOG("Error: {}", file_size_and_count_diff.error());
    return false;
  }

  return true;
}

static std::string
rewrite_stdout_from_compiler(const Context& ctx, std::string&& stdout_data)
{
  using util::Tokenizer;
  using Mode = Tokenizer::Mode;
  using IncludeDelimiter = Tokenizer::IncludeDelimiter;
  if (!stdout_data.empty()) {
    std::string new_stdout_text;
    for (const auto line : Tokenizer(
           stdout_data, "\n", Mode::include_empty, IncludeDelimiter::yes)) {
      if (util::starts_with(line, "__________")) {
        Util::send_to_fd(ctx, std::string(line), STDOUT_FILENO);
      }
      // Ninja uses the lines with 'Note: including file: ' to determine the
      // used headers. Headers within basedir need to be changed into relative
      // paths because otherwise Ninja will use the abs path to original header
      // to check if a file needs to be recompiled.
      else if (ctx.config.compiler_type() == CompilerType::msvc
               && !ctx.config.base_dir().empty()
               && util::starts_with(line, "Note: including file:")) {
        std::string orig_line(line.data(), line.length());
        std::string abs_inc_path =
          util::replace_first(orig_line, "Note: including file:", "");
        abs_inc_path = util::strip_whitespace(abs_inc_path);
        std::string rel_inc_path = Util::make_relative_path(
          ctx, Util::normalize_concrete_absolute_path(abs_inc_path));
        std::string line_with_rel_inc =
          util::replace_first(orig_line, abs_inc_path, rel_inc_path);
        new_stdout_text.append(line_with_rel_inc);
      } else {
        new_stdout_text.append(line.data(), line.length());
      }
    }
    return new_stdout_text;
  } else {
    return std::move(stdout_data);
  }
}

// Run the real compiler and put the result in cache. Returns the result key.
static nonstd::expected<Digest, Failure>
to_cache(Context& ctx,
         Args& args,
         std::optional<Digest> result_key,
         const Args& depend_extra_args,
         Hash* depend_mode_hash)
{
  if (ctx.config.is_compiler_group_msvc()) {
    args.push_back(fmt::format("-Fo{}", ctx.args_info.output_obj));
  } else {
    args.push_back("-o");
    args.push_back(ctx.args_info.output_obj);
  }

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
      return nonstd::make_unexpected(Statistic::bad_output_file);
    }
  }

  LOG_RAW("Running real compiler");
  MTR_BEGIN("execute", "compiler");

  nonstd::expected<DoExecuteResult, Failure> result;
  if (!ctx.config.depend_mode()) {
    result = do_execute(ctx, args);
    args.pop_back(3);
  } else {
    // Use the original arguments (including dependency options) in depend
    // mode.
    Args depend_mode_args = ctx.orig_args;
    depend_mode_args.erase_with_prefix("--ccache-");
    depend_mode_args.push_back(depend_extra_args);
    add_prefix(ctx, depend_mode_args, ctx.config.prefix_command());

    ctx.time_of_compilation = time(nullptr);
    result = do_execute(ctx, depend_mode_args);
  }
  MTR_END("execute", "compiler");

  if (!result) {
    return nonstd::make_unexpected(result.error());
  }

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler.
  if (!ctx.cpp_stderr_data.empty()) {
    result->stderr_data = ctx.cpp_stderr_data + result->stderr_data;
  }

  result->stdout_data =
    rewrite_stdout_from_compiler(ctx, std::move(result->stdout_data));

  if (result->exit_status != 0) {
    LOG("Compiler gave exit status {}", result->exit_status);

    // We can output stderr immediately instead of rerunning the compiler.
    Util::send_to_fd(ctx, result->stderr_data, STDERR_FILENO);
    Util::send_to_fd(ctx, result->stdout_data, STDOUT_FILENO);

    auto failure = Failure(Statistic::compile_failed);
    failure.set_exit_code(result->exit_status);
    return nonstd::make_unexpected(failure);
  }

  if (ctx.config.depend_mode()) {
    ASSERT(depend_mode_hash);
    result_key = result_key_from_depfile(ctx, *depend_mode_hash);
    if (!result_key) {
      return nonstd::make_unexpected(Statistic::internal_error);
    }
  }

  ASSERT(result_key);

  bool produce_dep_file = ctx.args_info.generating_dependencies
                          && ctx.args_info.output_dep != "/dev/null";

  if (produce_dep_file) {
    Depfile::make_paths_relative_in_output_dep(ctx);
  }

  Stat obj_stat;
  if (!ctx.args_info.expect_output_obj) {
    // Don't probe for object file when we don't expect one since we otherwise
    // will be fooled by an already existing object file.
    LOG_RAW("Compiler not expected to produce an object file");
  } else {
    obj_stat = Stat::stat(ctx.args_info.output_obj);
    if (!obj_stat) {
      LOG_RAW("Compiler didn't produce an object file");
      return nonstd::make_unexpected(Statistic::compiler_produced_no_output);
    } else if (obj_stat.size() == 0) {
      LOG_RAW("Compiler produced an empty object file");
      return nonstd::make_unexpected(Statistic::compiler_produced_empty_output);
    }
  }

  MTR_BEGIN("result", "result_put");
  const bool added = ctx.storage.put(
    *result_key, core::CacheEntryType::result, [&](const auto& path) {
      return write_result(
        ctx, path, obj_stat, result->stdout_data, result->stderr_data);
    });
  MTR_END("result", "result_put");
  if (!added) {
    return nonstd::make_unexpected(Statistic::internal_error);
  }

  // Everything OK.
  Util::send_to_fd(ctx, result->stderr_data, STDERR_FILENO);
  // Send stdout after stderr, it makes the output clearer with MSVC.
  Util::send_to_fd(ctx, result->stdout_data, STDOUT_FILENO);

  return *result_key;
}

// Find the result key by running the compiler in preprocessor mode and
// hashing the result.
static nonstd::expected<Digest, Failure>
get_result_key_from_cpp(Context& ctx, Args& args, Hash& hash)
{
  ctx.time_of_compilation = time(nullptr);

  std::string preprocessed_path;
  std::string cpp_stderr_data;

  if (ctx.args_info.direct_i_file) {
    // We are compiling a .i or .ii file - that means we can skip the cpp stage
    // and directly form the correct i_tmpfile.
    preprocessed_path = ctx.args_info.input_file;
    cpp_stderr_data = "";
  } else {
    // Run cpp on the input file to obtain the .i.

    // preprocessed_path needs the proper cpp_extension for the compiler to do
    // its thing correctly.
    TemporaryFile tmp_stdout(
      FMT("{}/tmp.cpp_stdout", ctx.config.temporary_dir()),
      FMT(".{}", ctx.config.cpp_extension()));
    preprocessed_path = tmp_stdout.path;
    tmp_stdout.fd.close(); // We're only using the path.
    ctx.register_pending_tmp_file(preprocessed_path);

    const size_t orig_args_size = args.size();
    args.push_back("-E");
    if (ctx.config.keep_comments_cpp()) {
      args.push_back("-C");
    }

    // Pass -o instead of sending the preprocessor output to stdout to work
    // around compilers that don't exit with a proper status on write error to
    // stdout. See also <https://github.com/llvm/llvm-project/issues/56499>.
    args.push_back("-o");
    args.push_back(preprocessed_path);

    args.push_back(ctx.args_info.input_file);

    add_prefix(ctx, args, ctx.config.prefix_command_cpp());
    LOG_RAW("Running preprocessor");
    MTR_BEGIN("execute", "preprocessor");
    const auto result = do_execute(ctx, args, false);
    MTR_END("execute", "preprocessor");
    args.pop_back(args.size() - orig_args_size);

    if (!result) {
      return nonstd::make_unexpected(result.error());
    } else if (result->exit_status != 0) {
      LOG("Preprocessor gave exit status {}", result->exit_status);
      return nonstd::make_unexpected(Statistic::preprocessor_error);
    }

    cpp_stderr_data = result->stderr_data;
  }

  hash.hash_delimiter("cpp");
  TRY(process_preprocessed_file(ctx, hash, preprocessed_path));

  hash.hash_delimiter("cppstderr");
  hash.hash(cpp_stderr_data);

  ctx.i_tmpfile = preprocessed_path;

  if (!ctx.config.run_second_cpp()) {
    // If we are using the CPP trick, we need to remember this stderr data and
    // output it just before the main stderr from the compiler pass.
    ctx.cpp_stderr_data = std::move(cpp_stderr_data);
    hash.hash_delimiter("runsecondcpp");
    hash.hash("false");
  }

  return hash.digest();
}

// Hash mtime or content of a file, or the output of a command, according to
// the CCACHE_COMPILERCHECK setting.
static nonstd::expected<void, Failure>
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
  } else if (util::starts_with(ctx.config.compiler_check(), "string:")) {
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
      return nonstd::make_unexpected(Statistic::compiler_check_failed);
    }
  }
  return {};
}

// Hash the host compiler(s) invoked by nvcc.
//
// If `ccbin_st` and `ccbin` are set, they refer to a directory or compiler set
// with -ccbin/--compiler-bindir. If `ccbin_st` is nullptr or `ccbin` is the
// empty string, the compilers are looked up in PATH instead.
static nonstd::expected<void, Failure>
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
          TRY(hash_compiler(ctx, hash, st, path, false));
        }
      } else {
        std::string path = find_executable(ctx, compiler, ctx.orig_args[0]);
        if (!path.empty()) {
          auto st = Stat::stat(path, Stat::OnError::log);
          TRY(hash_compiler(ctx, hash, st, ccbin, false));
        }
      }
    }
  } else {
    TRY(hash_compiler(ctx, hash, *ccbin_st, ccbin, false));
  }

  return {};
}

// update a hash with information common for the direct and preprocessor modes.
static nonstd::expected<void, Failure>
hash_common_info(const Context& ctx,
                 const Args& args,
                 Hash& hash,
                 const ArgsInfo& args_info)
{
  hash.hash(HASH_PREFIX);

  if (!ctx.config.namespace_().empty()) {
    hash.hash_delimiter("namespace");
    hash.hash(ctx.config.namespace_());
  }

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
    return nonstd::make_unexpected(Statistic::could_not_find_compiler);
  }

  // Hash information about the compiler.
  TRY(hash_compiler(ctx, hash, st, compiler_path, true));

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

  if (!(ctx.config.sloppiness().is_enabled(core::Sloppy::locale))) {
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
        if (util::starts_with(ctx.apparent_cwd, old_path)) {
          dir_to_hash = new_path + ctx.apparent_cwd.substr(old_path.size());
        }
      }
    }
    LOG("Hashing CWD {}", dir_to_hash);
    hash.hash_delimiter("cwd");
    hash.hash(dir_to_hash);
  }

  // The object file produced by MSVC includes the full path to the source file
  // even without debug flags. Hashing the directory should be enough since the
  // filename is included in the hash anyway.
  if (ctx.config.is_compiler_group_msvc() && ctx.config.hash_dir()) {
    const std::string output_obj_dir =
      util::is_absolute_path(args_info.output_obj)
        ? std::string(Util::dir_name(args_info.output_obj))
        : ctx.actual_cwd;
    LOG("Hashing object file directory {}", output_obj_dir);
    hash.hash_delimiter("source path");
    hash.hash(output_obj_dir);
  }

  if (ctx.args_info.seen_split_dwarf || ctx.args_info.profile_arcs) {
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

  if (ctx.args_info.generating_coverage
      && !(ctx.config.sloppiness().is_enabled(core::Sloppy::gcno_cwd))) {
    // GCC 9+ includes $PWD in the .gcno file. Since we don't have knowledge
    // about compiler version we always (unless sloppiness is wanted) include
    // the directory in the hash for now.
    LOG_RAW("Hashing apparent CWD due to generating a .gcno file");
    hash.hash_delimiter("CWD in .gcno");
    hash.hash(ctx.apparent_cwd);
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
    std::string_view stem =
      Util::remove_extension(Util::base_name(ctx.args_info.output_obj));
    std::string gcda_path = FMT("{}/{}.gcda", dir, stem);
    LOG("Hashing coverage path {}", gcda_path);
    hash.hash_delimiter("gcda");
    hash.hash(gcda_path);
  }

  // Possibly hash the sanitize blacklist file path.
  for (const auto& sanitize_blacklist : args_info.sanitize_blacklists) {
    LOG("Hashing sanitize blacklist {}", sanitize_blacklist);
    hash.hash_delimiter("sanitizeblacklist");
    if (!hash_binary_file(ctx, hash, sanitize_blacklist)) {
      return nonstd::make_unexpected(Statistic::error_hashing_extra_file);
    }
  }

  if (!ctx.config.extra_files_to_hash().empty()) {
    for (const std::string& path :
         util::split_path_list(ctx.config.extra_files_to_hash())) {
      LOG("Hashing extra file {}", path);
      hash.hash_delimiter("extrafile");
      if (!hash_binary_file(ctx, hash, path)) {
        return nonstd::make_unexpected(Statistic::error_hashing_extra_file);
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

  return {};
}

static bool
hash_profile_data_file(const Context& ctx, Hash& hash)
{
  const std::string& profile_path = ctx.args_info.profile_path;
  std::string_view base_name = Util::remove_extension(ctx.args_info.output_obj);
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
    patterns.cbegin(), patterns.cend(), [&arg](const auto& pattern) {
      const auto& prefix =
        std::string_view(pattern).substr(0, pattern.length() - 1);
      return (
        pattern == arg
        || (util::ends_with(pattern, "*") && util::starts_with(arg, prefix)));
    });
}

static std::tuple<std::optional<std::string_view>,
                  std::optional<std::string_view>>
get_option_and_value(std::string_view option, const Args& args, size_t& i)
{
  if (args[i] == option) {
    if (i + 1 < args.size()) {
      ++i;
      return {option, args[i]};
    } else {
      return {std::nullopt, std::nullopt};
    }
  } else if (util::starts_with(args[i], option)) {
    return {option, std::string_view(args[i]).substr(option.length())};
  } else {
    return {std::nullopt, std::nullopt};
  }
}

// Update a hash sum with information specific to the direct and preprocessor
// modes and calculate the result key. Returns the result key on success, and
// if direct_mode is true also the manifest key.
static nonstd::expected<std::pair<std::optional<Digest>, std::optional<Digest>>,
                        Failure>
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
    hash.hash(core::Manifest::k_format_version);
  }

  // clang will emit warnings for unused linker flags, so we shouldn't skip
  // those arguments.
  int is_clang = ctx.config.is_compiler_group_clang()
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
    if (util::starts_with(args[i], "-L") && !is_clang) {
      continue;
    }

    // -Wl,... doesn't affect compilation (except for clang).
    if (util::starts_with(args[i], "-Wl,") && !is_clang) {
      continue;
    }

    // The -fdebug-prefix-map option may be used in combination with
    // CCACHE_BASEDIR to reuse results across different directories. Skip using
    // the value of the option from hashing but still hash the existence of the
    // option.
    if (util::starts_with(args[i], "-fdebug-prefix-map=")) {
      hash.hash_delimiter("arg");
      hash.hash("-fdebug-prefix-map=");
      continue;
    }
    if (util::starts_with(args[i], "-ffile-prefix-map=")) {
      hash.hash_delimiter("arg");
      hash.hash("-ffile-prefix-map=");
      continue;
    }
    if (util::starts_with(args[i], "-fmacro-prefix-map=")) {
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

    if (ctx.args_info.generating_dependencies) {
      std::optional<std::string_view> option;
      std::optional<std::string_view> value;

      if (util::starts_with(args[i], "-Wp,")) {
        // Skip the dependency filename since it doesn't impact the output.
        if (util::starts_with(args[i], "-Wp,-MD,")
            && args[i].find(',', 8) == std::string::npos) {
          hash.hash(args[i].data(), 8);
          continue;
        } else if (util::starts_with(args[i], "-Wp,-MMD,")
                   && args[i].find(',', 9) == std::string::npos) {
          hash.hash(args[i].data(), 9);
          continue;
        }
      } else if (std::tie(option, value) = get_option_and_value("-MF", args, i);
                 option) {
        // Skip the dependency filename since it doesn't impact the output.
        hash.hash(*option);
        continue;
      } else if (std::tie(option, value) = get_option_and_value("-MQ", args, i);
                 option) {
        hash.hash(*option);
        // No need to hash the dependency target since we always calculate it on
        // a cache hit.
        continue;
      } else if (std::tie(option, value) = get_option_and_value("-MT", args, i);
                 option) {
        hash.hash(*option);
        // No need to hash the dependency target since we always calculate it on
        // a cache hit.
        continue;
      }
    }

    if (util::starts_with(args[i], "-specs=")
        || util::starts_with(args[i], "--specs=")
        || (args[i] == "-specs" || args[i] == "--specs")
        || args[i] == "--config") {
      std::string path;
      size_t eq_pos = args[i].find('=');
      if (eq_pos == std::string::npos) {
        if (i + 1 >= args.size()) {
          LOG("missing argument for \"{}\"", args[i]);
          return nonstd::make_unexpected(Statistic::bad_compiler_arguments);
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
        TRY(hash_compiler(ctx, hash, st, path, false));
        continue;
      }
    }

    if (util::starts_with(args[i], "-fplugin=")) {
      auto st = Stat::stat(&args[i][9], Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        TRY(hash_compiler(ctx, hash, st, &args[i][9], false));
        continue;
      }
    }

    if (args[i] == "-Xclang" && i + 3 < args.size() && args[i + 1] == "-load"
        && args[i + 2] == "-Xclang") {
      auto st = Stat::stat(args[i + 3], Stat::OnError::log);
      if (st) {
        hash.hash_delimiter("plugin");
        TRY(hash_compiler(ctx, hash, st, args[i + 3], false));
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
        TRY(hash_nvcc_host_compiler(ctx, hash, &st, args[i + 1]));
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
    TRY(hash_nvcc_host_compiler(ctx, hash));
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
    return nonstd::make_unexpected(Statistic::no_input_file);
  }

  // Adding -arch to hash since cpp output is affected.
  for (const auto& arch : ctx.args_info.arch_args) {
    hash.hash_delimiter("-arch");
    hash.hash(arch);
  }

  std::optional<Digest> result_key;
  std::optional<Digest> manifest_key;

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

    hash.hash_delimiter("sourcecode hash");
    Digest input_file_digest;
    int result =
      hash_source_code_file(ctx, input_file_digest, ctx.args_info.input_file);
    if (result & HASH_SOURCE_CODE_ERROR) {
      return nonstd::make_unexpected(Statistic::internal_error);
    }
    if (result & HASH_SOURCE_CODE_FOUND_TIME) {
      LOG_RAW("Disabling direct mode");
      ctx.config.set_direct_mode(false);
      return std::make_pair(std::nullopt, std::nullopt);
    }
    hash.hash(input_file_digest.to_string());

    manifest_key = hash.digest();

    auto manifest_path = ctx.storage.get(*manifest_key,
                                         core::CacheEntryType::manifest,
                                         storage::Storage::Mode::primary_only);

    if (manifest_path) {
      LOG("Looking for result key in {}", *manifest_path);
      MTR_BEGIN("manifest", "manifest_get");
      try {
        const auto manifest = read_manifest(*manifest_path);
        result_key = manifest.look_up_result_digest(ctx);
      } catch (const core::Error& e) {
        LOG("Failed to look up result key in {}: {}", *manifest_path, e.what());
      }
      MTR_END("manifest", "manifest_get");
      if (result_key) {
        LOG_RAW("Got result key from manifest");
      } else {
        LOG_RAW("Did not find result key in manifest");
      }
    }
    // Check secondary storage if not found in primary
    if (!result_key) {
      manifest_path = ctx.storage.get(*manifest_key,
                                      core::CacheEntryType::manifest,
                                      storage::Storage::Mode::secondary_only);
      if (manifest_path) {
        LOG("Looking for result key in fetched secondary manifest {}",
            *manifest_path);
        MTR_BEGIN("manifest", "secondary_manifest_get");
        try {
          const auto manifest = read_manifest(*manifest_path);
          result_key = manifest.look_up_result_digest(ctx);
        } catch (const core::Error& e) {
          LOG(
            "Failed to look up result key in {}: {}", *manifest_path, e.what());
        }
        MTR_END("manifest", "secondary_manifest_get");
        if (result_key) {
          LOG_RAW("Got result key from fetched secondary manifest");
        } else {
          LOG_RAW("Did not find result key in fetched secondary manifest");
        }
      }
    }
  } else if (ctx.args_info.arch_args.empty()) {
    const auto digest = get_result_key_from_cpp(ctx, preprocessor_args, hash);
    if (!digest) {
      return nonstd::make_unexpected(digest.error());
    }
    result_key = *digest;
    LOG_RAW("Got result key from preprocessor");
  } else {
    preprocessor_args.push_back("-arch");
    for (size_t i = 0; i < ctx.args_info.arch_args.size(); ++i) {
      preprocessor_args.push_back(ctx.args_info.arch_args[i]);
      const auto digest = get_result_key_from_cpp(ctx, preprocessor_args, hash);
      if (!digest) {
        return nonstd::make_unexpected(digest.error());
      }
      result_key = *digest;
      LOG("Got result key from preprocessor with -arch {}",
          ctx.args_info.arch_args[i]);
      if (i != ctx.args_info.arch_args.size() - 1) {
        result_key = std::nullopt;
      }
      preprocessor_args.pop_back();
    }
    preprocessor_args.pop_back();
  }

  return std::make_pair(result_key, manifest_key);
}

enum class FromCacheCallMode { direct, cpp };

// Try to return the compile result from cache.
static nonstd::expected<bool, Failure>
from_cache(Context& ctx, FromCacheCallMode mode, const Digest& result_key)
{
  UmaskScope umask_scope(ctx.original_umask);

  // The user might be disabling cache hits.
  if (ctx.config.recache()) {
    return false;
  }

  // If we're using Clang, we can't trust a precompiled header object based on
  // running the preprocessor since clang will produce a fatal error when the
  // precompiled header is used and one of the included files has an updated
  // timestamp:
  //
  //     file 'foo.h' has been modified since the precompiled header 'foo.pch'
  //     was built
  if ((ctx.config.is_compiler_group_clang()
       || ctx.config.compiler_type() == CompilerType::other)
      && ctx.args_info.output_is_precompiled_header
      && mode == FromCacheCallMode::cpp) {
    LOG_RAW("Not considering cached precompiled header in preprocessor mode");
    return false;
  }

  MTR_SCOPE("cache", "from_cache");

  // Get result from cache.
  const auto result_path =
    ctx.storage.get(result_key, core::CacheEntryType::result);
  if (!result_path) {
    return false;
  }

  try {
    File file(*result_path, "rb");
    core::FileReader file_reader(file.get());
    core::CacheEntryReader cache_entry_reader(file_reader);
    Result::Reader result_reader(cache_entry_reader, *result_path);
    ResultRetriever result_retriever(ctx);

    result_reader.read(result_retriever);
  } catch (ResultRetriever::WriteError& e) {
    LOG(
      "Write error when retrieving result from {}: {}", *result_path, e.what());
    return nonstd::make_unexpected(Statistic::bad_output_file);
  } catch (core::Error& e) {
    LOG("Failed to get result from {}: {}", *result_path, e.what());
    return false;
  }

  LOG_RAW("Succeeded getting cached result");
  return true;
}

// Find the real compiler and put it into ctx.orig_args[0]. We just search the
// PATH to find an executable of the same name that isn't ourselves.
void
find_compiler(Context& ctx,
              const FindExecutableFunction& find_executable_function)
{
  // gcc --> 0
  // ccache gcc --> 1
  // ccache ccache gcc --> 2
  size_t compiler_pos = 0;
  while (compiler_pos < ctx.orig_args.size()
         && Util::is_ccache_executable(ctx.orig_args[compiler_pos])) {
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
      : find_executable_function(ctx, compiler, ctx.orig_args[0]);

  if (resolved_compiler.empty()) {
    throw core::Fatal("Could not find compiler \"{}\" in PATH", compiler);
  }

  if (Util::is_ccache_executable(resolved_compiler)) {
    throw core::Fatal("Recursive invocation of ccache");
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

  LOG("Primary configuration file: {}", ctx.config.primary_config_path());
  LOG("Secondary configuration file: {}", ctx.config.secondary_config_path());

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
static nonstd::expected<void, Failure>
set_up_uncached_err()
{
  int uncached_fd =
    dup(STDERR_FILENO); // The file descriptor is intentionally leaked.
  if (uncached_fd == -1) {
    LOG("dup(2) failed: {}", strerror(errno));
    return nonstd::make_unexpected(Statistic::internal_error);
  }

  Util::setenv("UNCACHED_ERR_FD", FMT("{}", uncached_fd));
  return {};
}

static int cache_compilation(int argc, const char* const* argv);

static nonstd::expected<core::StatisticsCounters, Failure>
do_cache_compilation(Context& ctx, const char* const* argv);

static void
log_result_to_debug_log(Context& ctx)
{
  if (ctx.config.log_file().empty() && !ctx.config.debug()) {
    return;
  }

  core::Statistics statistics(ctx.storage.primary.get_statistics_updates());
  for (const auto& message : statistics.get_statistics_ids()) {
    LOG("Result: {}", message);
  }
}

static void
log_result_to_stats_log(Context& ctx)
{
  if (ctx.config.stats_log().empty()) {
    return;
  }

  core::Statistics statistics(ctx.storage.primary.get_statistics_updates());
  const auto ids = statistics.get_statistics_ids();
  if (ids.empty()) {
    return;
  }

  core::StatsLog(ctx.config.stats_log())
    .log_result(ctx.args_info.input_file, ids);
}

static void
finalize_at_exit(Context& ctx)
{
  try {
    if (ctx.config.disable()) {
      // Just log result, don't update statistics.
      LOG_RAW("Result: disabled");
      return;
    }

    log_result_to_debug_log(ctx);
    log_result_to_stats_log(ctx);

    ctx.storage.finalize();
  } catch (const core::ErrorBase& e) {
    // finalize_at_exit must not throw since it's called by a destructor.
    LOG("Error while finalizing stats: {}", e.what());
  }

  // Dump log buffer last to not lose any logs.
  if (ctx.config.debug() && !ctx.args_info.output_obj.empty()) {
    Logging::dump_log(prepare_debug_path(ctx.config.debug_dir(),
                                         ctx.time_of_invocation,
                                         ctx.args_info.output_obj,
                                         "log"));
  }
}

// The entry point when invoked to cache a compilation.
static int
cache_compilation(int argc, const char* const* argv)
{
  tzset(); // Needed for localtime_r.

  bool fall_back_to_original_compiler = false;
  Args saved_orig_args;
  std::optional<uint32_t> original_umask;
  std::string saved_temp_dir;

  {
    Context ctx;
    ctx.initialize();
    SignalHandler signal_handler(ctx);
    Finalizer finalizer([&ctx] { finalize_at_exit(ctx); });

    initialize(ctx, argc, argv);

    MTR_BEGIN("main", "find_compiler");
    find_compiler(ctx, &find_executable);
    MTR_END("main", "find_compiler");

    const auto result = do_cache_compilation(ctx, argv);
    const auto& counters = result ? *result : result.error().counters();
    ctx.storage.primary.increment_statistics(counters);
    if (!result) {
      if (result.error().exit_code()) {
        return *result.error().exit_code();
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
      Util::set_umask(*original_umask);
    }
    auto execv_argv = saved_orig_args.to_argv();
    execute_noreturn(execv_argv.data(), saved_temp_dir);
    throw core::Fatal(
      "execute_noreturn of {} failed: {}", execv_argv[0], strerror(errno));
  }

  return EXIT_SUCCESS;
}

static nonstd::expected<core::StatisticsCounters, Failure>
do_cache_compilation(Context& ctx, const char* const* argv)
{
  if (ctx.actual_cwd.empty()) {
    LOG("Unable to determine current working directory: {}", strerror(errno));
    return nonstd::make_unexpected(Statistic::internal_error);
  }

  if (!ctx.config.log_file().empty() || ctx.config.debug()) {
    ctx.config.visit_items([&ctx](const std::string& key,
                                  const std::string& value,
                                  const std::string& origin) {
      const auto& log_value =
        key == "secondary_storage"
          ? ctx.storage.get_secondary_storage_config_for_logging()
          : value;
      BULK_LOG("Config: ({}) {} = {}", origin, key, log_value);
    });
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
    return nonstd::make_unexpected(Statistic::none);
  }

  LOG("Command line: {}", Util::format_argv_for_logging(argv));
  LOG("Hostname: {}", Util::get_hostname());
  LOG("Working directory: {}", ctx.actual_cwd);
  if (ctx.apparent_cwd != ctx.actual_cwd) {
    LOG("Apparent working directory: {}", ctx.apparent_cwd);
  }

  LOG("Compiler type: {}", compiler_type_to_string(ctx.config.compiler_type()));

  // Set CCACHE_DISABLE so no process ccache executes from now on will risk
  // calling ccache second time. For instance, if the real compiler is a wrapper
  // script that calls "ccache $compiler ..." we want that inner ccache call to
  // be disabled.
  Util::setenv("CCACHE_DISABLE", "1");

  MTR_BEGIN("main", "process_args");
  ProcessArgsResult processed = process_args(ctx);
  MTR_END("main", "process_args");

  if (processed.error) {
    return nonstd::make_unexpected(*processed.error);
  }

  TRY(set_up_uncached_err());

  for (const auto& name : {"DEPENDENCIES_OUTPUT", "SUNPRO_DEPENDENCIES"}) {
    if (getenv(name)) {
      LOG("Unsupported environment variable: {}", name);
      return Statistic::unsupported_environment_variable;
    }
  }

  if (ctx.config.is_compiler_group_msvc()) {
    for (const auto& name : {"CL", "_CL_"}) {
      if (getenv(name)) {
        LOG("Unsupported environment variable: {}", name);
        return Statistic::unsupported_environment_variable;
      }
    }
  }

  if (!ctx.config.run_second_cpp() && ctx.config.is_compiler_group_msvc()) {
    LOG_RAW("Second preprocessor cannot be disabled");
    ctx.config.set_run_second_cpp(true);
  }

  if (ctx.config.depend_mode()
      && (!ctx.args_info.generating_dependencies
          || ctx.args_info.output_dep == "/dev/null"
          || !ctx.config.run_second_cpp())) {
    LOG_RAW("Disabling depend mode");
    ctx.config.set_depend_mode(false);
  }

  if (ctx.storage.has_secondary_storage()) {
    if (ctx.config.file_clone()) {
      LOG_RAW("Disabling file clone mode since secondary storage is enabled");
      ctx.config.set_file_clone(false);
    }
    if (ctx.config.hard_link()) {
      LOG_RAW("Disabling hard link mode since secondary storage is enabled");
      ctx.config.set_hard_link(false);
    }
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
    const auto path = prepare_debug_path(ctx.config.debug_dir(),
                                         ctx.time_of_invocation,
                                         ctx.args_info.orig_output_obj,
                                         "input-text");
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

  {
    MTR_SCOPE("hash", "common_hash");
    TRY(hash_common_info(
      ctx, processed.preprocessor_args, common_hash, ctx.args_info));
  }

  if (processed.hash_actual_cwd) {
    common_hash.hash_delimiter("actual_cwd");
    common_hash.hash(ctx.actual_cwd);
  }

  // Try to find the hash using the manifest.
  Hash direct_hash = common_hash;
  init_hash_debug(ctx, direct_hash, 'd', "DIRECT MODE", debug_text_file);

  Args args_to_hash = processed.preprocessor_args;
  args_to_hash.push_back(processed.extra_args_to_hash);

  bool put_result_in_manifest = false;
  std::optional<Digest> result_key;
  std::optional<Digest> result_key_from_manifest;
  std::optional<Digest> manifest_key;

  if (ctx.config.direct_mode()) {
    LOG_RAW("Trying direct lookup");
    Args dummy_args;
    MTR_BEGIN("hash", "direct_hash");
    const auto result_and_manifest_key = calculate_result_and_manifest_key(
      ctx, args_to_hash, dummy_args, direct_hash, true);
    MTR_END("hash", "direct_hash");
    if (!result_and_manifest_key) {
      return nonstd::make_unexpected(result_and_manifest_key.error());
    }
    std::tie(result_key, manifest_key) = *result_and_manifest_key;
    if (result_key) {
      // If we can return from cache at this point then do so.
      const auto from_cache_result =
        from_cache(ctx, FromCacheCallMode::direct, *result_key);
      if (!from_cache_result) {
        return nonstd::make_unexpected(from_cache_result.error());
      } else if (*from_cache_result) {
        return Statistic::direct_cache_hit;
      }

      // Wasn't able to return from cache at this point. However, the result
      // was already found in manifest, so don't re-add it later.
      put_result_in_manifest = false;

      result_key_from_manifest = result_key;
    } else {
      // Add result to manifest later.
      put_result_in_manifest = true;
    }

    if (!ctx.config.recache()) {
      ctx.storage.primary.increment_statistic(Statistic::direct_cache_miss);
    }
  }

  if (ctx.config.read_only_direct()) {
    LOG_RAW("Read-only direct mode; running real compiler");
    return nonstd::make_unexpected(Statistic::cache_miss);
  }

  if (!ctx.config.depend_mode()) {
    // Find the hash using the preprocessed output. Also updates
    // ctx.included_files.
    Hash cpp_hash = common_hash;
    init_hash_debug(ctx, cpp_hash, 'p', "PREPROCESSOR MODE", debug_text_file);

    MTR_BEGIN("hash", "cpp_hash");
    const auto result_and_manifest_key = calculate_result_and_manifest_key(
      ctx, args_to_hash, processed.preprocessor_args, cpp_hash, false);
    MTR_END("hash", "cpp_hash");
    if (!result_and_manifest_key) {
      return nonstd::make_unexpected(result_and_manifest_key.error());
    }
    result_key = result_and_manifest_key->first;

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
    const auto from_cache_result =
      from_cache(ctx, FromCacheCallMode::cpp, *result_key);
    if (!from_cache_result) {
      return nonstd::make_unexpected(from_cache_result.error());
    } else if (*from_cache_result) {
      if (ctx.config.direct_mode() && manifest_key && put_result_in_manifest) {
        update_manifest_file(ctx, *manifest_key, *result_key);
      }
      return Statistic::preprocessed_cache_hit;
    }

    ctx.storage.primary.increment_statistic(Statistic::preprocessed_cache_miss);
  }

  if (ctx.config.read_only()) {
    LOG_RAW("Read-only mode; running real compiler");
    return nonstd::make_unexpected(Statistic::cache_miss);
  }

  add_prefix(ctx, processed.compiler_args, ctx.config.prefix_command());

  // In depend_mode, extend the direct hash.
  Hash* depend_mode_hash = ctx.config.depend_mode() ? &direct_hash : nullptr;

  // Run real compiler, sending output to cache.
  MTR_BEGIN("cache", "to_cache");
  const auto digest = to_cache(ctx,
                               processed.compiler_args,
                               result_key,
                               ctx.args_info.depend_extra_args,
                               depend_mode_hash);
  MTR_END("cache", "to_cache");
  if (!digest) {
    return nonstd::make_unexpected(digest.error());
  }
  result_key = *digest;
  if (ctx.config.direct_mode()) {
    ASSERT(manifest_key);
    MTR_SCOPE("cache", "update_manifest");
    update_manifest_file(ctx, *manifest_key, *result_key);
  }

  return ctx.config.recache() ? Statistic::recache : Statistic::cache_miss;
}

int
ccache_main(int argc, const char* const* argv)
{
  try {
    if (Util::is_ccache_executable(argv[0])) {
      if (argc < 2) {
        PRINT_RAW(stderr, core::get_usage_text(Util::base_name(argv[0])));
        exit(EXIT_FAILURE);
      }
      // If the first argument isn't an option, then assume we are being
      // passed a compiler name and options.
      if (argv[1][0] == '-') {
        return core::process_main_options(argc, argv);
      }
    }

    return cache_compilation(argc, argv);
  } catch (const core::ErrorBase& e) {
    PRINT(stderr, "ccache: error: {}\n", e.what());
    return EXIT_FAILURE;
  }
}
