// Copyright (C) 2009-2023 Joel Rosdahl and other contributors
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

#include "hashutil.hpp"

#include "Args.hpp"
#include "Config.hpp"
#include "Context.hpp"
#include "execute.hpp"
#include "macroskip.hpp"

#include <core/exceptions.hpp>
#include <util/DirEntry.hpp>
#include <util/file.hpp>
#include <util/fmtmacros.hpp>
#include <util/logging.hpp>
#include <util/string.hpp>
#include <util/time.hpp>
#include <util/wincompat.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include "InodeCache.hpp"
#endif

#include "third_party/blake3/blake3_cpu_supports_avx2.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#ifdef HAVE_AVX2
#  include <immintrin.h>
#endif

namespace {

// Pre-condition: str[pos - 1] == '_'
HashSourceCode
check_for_temporal_macros_helper(std::string_view str, size_t pos)
{
  if (pos + 7 > str.length()) {
    return HashSourceCode::ok;
  }

  HashSourceCode found = HashSourceCode::ok;
  int macro_len = 7;
  if (memcmp(&str[pos], "_DATE__", 7) == 0) {
    found = HashSourceCode::found_date;
  } else if (memcmp(&str[pos], "_TIME__", 7) == 0) {
    found = HashSourceCode::found_time;
  } else if (pos + 12 <= str.length()
             && memcmp(&str[pos], "_TIMESTAMP__", 12) == 0) {
    found = HashSourceCode::found_timestamp;
    macro_len = 12;
  } else {
    return HashSourceCode::ok;
  }

  // Check char before and after macro to verify that the found macro isn't part
  // of another identifier.
  if ((pos == 1 || (str[pos - 2] != '_' && !isalnum(str[pos - 2])))
      && (pos + macro_len == str.length()
          || (str[pos + macro_len] != '_' && !isalnum(str[pos + macro_len])))) {
    return found;
  }

  return HashSourceCode::ok;
}

HashSourceCodeResult
check_for_temporal_macros_bmh(std::string_view str, size_t start = 0)
{
  HashSourceCodeResult result;

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  size_t i = start + 7;

  while (i < str.length()) {
    // Check whether the substring ending at str[i] has the form "_....E..". On
    // the assumption that 'E' is less common in source than '_', we check
    // str[i-2] first.
    if (str[i - 2] == 'E' && str[i - 7] == '_') {
      result.insert(check_for_temporal_macros_helper(str, i - 6));
    }

    // macro_skip tells us how far we can skip forward upon seeing str[i] at
    // the end of a substring.
    i += macro_skip[(uint8_t)str[i]];
  }

  return result;
}

#ifdef HAVE_AVX2
#  ifndef _MSC_VER // MSVC does not need explicit enabling of AVX2.
HashSourceCodeResult check_for_temporal_macros_avx2(std::string_view str)
  __attribute__((target("avx2")));
#  endif

// The following algorithm, which uses AVX2 instructions to find __DATE__,
// __TIME__ and __TIMESTAMP__, is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
HashSourceCodeResult
check_for_temporal_macros_avx2(std::string_view str)
{
  HashSourceCodeResult result;

  // Set all 32 bytes in first and last to '_' and 'E' respectively.
  const __m256i first = _mm256_set1_epi8('_');
  const __m256i last = _mm256_set1_epi8('E');

  size_t pos = 0;
  for (; pos + 5 + 32 <= str.length(); pos += 32) {
    // Load 32 bytes from the current position in the input string, with
    // block_last being offset 5 bytes (i.e. the offset of 'E' in all three
    // macros).
    const __m256i block_first =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos]));
    const __m256i block_last =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos + 5]));

    // For i in 0..31:
    //   eq_X[i] = 0xFF if X[i] == block_X[i] else 0
    const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
    const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

    // Set bit i in mask if byte i in both eq_first and eq_last has the most
    // significant bit set.
    uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

    // A bit set in mask now indicates a possible location for a temporal macro.
    while (mask != 0) {
      // The start position + 1 (as we know the first char is _).
#  ifndef _MSC_VER
      const auto start = pos + __builtin_ctz(mask) + 1;
#  else
      unsigned long index;
      _BitScanForward(&index, mask);
      const auto start = pos + index + 1;
#  endif

      // Clear the least significant bit set.
      mask = mask & (mask - 1);

      result.insert(check_for_temporal_macros_helper(str, start));
    }
  }

  result.insert(check_for_temporal_macros_bmh(str, pos));

  return result;
}
#endif

HashSourceCodeResult
do_hash_file(const Context& ctx,
             Hash::Digest& digest,
             const std::string& path,
             size_t size_hint,
             bool check_temporal_macros)
{
#ifdef INODE_CACHE_SUPPORTED
  const InodeCache::ContentType content_type =
    check_temporal_macros ? InodeCache::ContentType::checked_for_temporal_macros
                          : InodeCache::ContentType::raw;
  if (ctx.config.inode_cache()) {
    const auto result = ctx.inode_cache.get(path, content_type);
    if (result) {
      digest = result->second;
      return result->first;
    }
  }
#else
  (void)ctx;
#endif

  const auto data = util::read_file<std::string>(path, size_hint);
  if (!data) {
    LOG("Failed to read {}: {}", path, data.error());
    return HashSourceCodeResult(HashSourceCode::error);
  }

  HashSourceCodeResult result;
  if (check_temporal_macros) {
    result.insert(check_for_temporal_macros(*data));
  }

  Hash hash;
  hash.hash(*data);
  digest = hash.digest();

#ifdef INODE_CACHE_SUPPORTED
  ctx.inode_cache.put(path, content_type, digest, result);
#endif

  return result;
}

} // namespace

HashSourceCodeResult
check_for_temporal_macros(std::string_view str)
{
#ifdef HAVE_AVX2
  if (blake3_cpu_supports_avx2()) {
    return check_for_temporal_macros_avx2(str);
  }
#endif
  return check_for_temporal_macros_bmh(str);
}

HashSourceCodeResult
hash_source_code_file(const Context& ctx,
                      Hash::Digest& digest,
                      const std::string& path,
                      size_t size_hint)
{
  const bool check_temporal_macros =
    !ctx.config.sloppiness().contains(core::Sloppy::time_macros);
  auto result =
    do_hash_file(ctx, digest, path, size_hint, check_temporal_macros);

  if (!check_temporal_macros || result.empty()
      || result.contains(HashSourceCode::error)) {
    return result;
  }

  if (result.contains(HashSourceCode::found_time)) {
    // We don't know for sure that the program actually uses the __TIME__ macro,
    // but we have to assume it anyway and hash the time stamp. However, that's
    // not very useful since the chance that we get a cache hit later the same
    // second should be quite slim... So, just signal back to the caller that
    // __TIME__ has been found so that the direct mode can be disabled.
    LOG("Found __TIME__ in {}", path);
    return result;
  }

  // __DATE__ or __TIMESTAMP__ found. We now make sure that the digest changes
  // if the (potential) expansion of those macros changes by computing a new
  // digest comprising the file digest and time information that represents the
  // macro expansions.

  Hash hash;
  hash.hash(util::format_digest(digest));

  if (result.contains(HashSourceCode::found_date)) {
    LOG("Found __DATE__ in {}", path);

    hash.hash_delimiter("date");
    auto now = util::localtime();
    if (!now) {
      result.insert(HashSourceCode::error);
      return result;
    }
    hash.hash(now->tm_year);
    hash.hash(now->tm_mon);
    hash.hash(now->tm_mday);

    // If the compiler has support for it, the expansion of __DATE__ will change
    // according to the value of SOURCE_DATE_EPOCH. Note: We have to hash both
    // SOURCE_DATE_EPOCH and the current date since we can't be sure that the
    // compiler honors SOURCE_DATE_EPOCH.
    const auto source_date_epoch = getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
      hash.hash(source_date_epoch);
    }
  }

  if (result.contains(HashSourceCode::found_timestamp)) {
    LOG("Found __TIMESTAMP__ in {}", path);

    util::DirEntry dir_entry(path);
    if (!dir_entry.is_regular_file()) {
      result.insert(HashSourceCode::error);
      return result;
    }

    auto modified_time = util::localtime(dir_entry.mtime());
    if (!modified_time) {
      result.insert(HashSourceCode::error);
      return result;
    }
    hash.hash_delimiter("timestamp");
#ifdef HAVE_ASCTIME_R
    char buffer[26];
    auto timestamp = asctime_r(&*modified_time, buffer);
#else
    auto timestamp = asctime(&*modified_time);
#endif
    if (!timestamp) {
      result.insert(HashSourceCode::error);
      return result;
    }
    hash.hash(timestamp);
  }

  digest = hash.digest();
  return result;
}

bool
hash_binary_file(const Context& ctx,
                 Hash::Digest& digest,
                 const std::string& path,
                 size_t size_hint)
{
  return do_hash_file(ctx, digest, path, size_hint, false).empty();
}

bool
hash_binary_file(const Context& ctx, Hash& hash, const std::string& path)
{
  Hash::Digest digest;
  const bool success = hash_binary_file(ctx, digest, path);
  if (success) {
    hash.hash(util::format_digest(digest));
  }
  return success;
}

bool
hash_command_output(Hash& hash,
                    const std::string& command,
                    const std::string& compiler)
{
#ifdef _WIN32
  std::string adjusted_command = util::strip_whitespace(command);

  // Add "echo" command.
  bool using_cmd_exe;
  if (util::starts_with(adjusted_command, "echo")) {
    adjusted_command = FMT("cmd.exe /c \"{}\"", adjusted_command);
    using_cmd_exe = true;
  } else if (util::starts_with(adjusted_command, "%compiler%")
             && compiler == "echo") {
    adjusted_command =
      FMT("cmd.exe /c \"{}{}\"", compiler, adjusted_command.substr(10));
    using_cmd_exe = true;
  } else {
    using_cmd_exe = false;
  }
  Args args = Args::from_string(adjusted_command);
#else
  Args args = Args::from_string(command);
#endif

  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "%compiler%") {
      args[i] = compiler;
    }
  }

  auto argv = args.to_argv();
  LOG("Executing compiler check command {}",
      util::format_argv_for_logging(argv.data()));

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  auto path = find_executable_in_path(args[0], getenv("PATH")).string();
  if (path.empty()) {
    path = args[0];
  }
  std::string sh = win32getshell(path);
  if (!sh.empty()) {
    path = sh;
  }

  si.cb = sizeof(STARTUPINFO);

  HANDLE pipe_out[2];
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  CreatePipe(&pipe_out[0], &pipe_out[1], &sa, 0);
  SetHandleInformation(pipe_out[0], HANDLE_FLAG_INHERIT, 0);
  si.hStdOutput = pipe_out[1];
  si.hStdError = pipe_out[1];
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  std::string win32args;
  if (using_cmd_exe) {
    win32args = adjusted_command; // quoted
  } else {
    win32args = util::format_argv_as_win32_command_string(argv.data(), sh);
  }
  BOOL ret = CreateProcess(path.c_str(),
                           const_cast<char*>(win32args.c_str()),
                           nullptr,
                           nullptr,
                           1,
                           0,
                           nullptr,
                           nullptr,
                           &si,
                           &pi);
  CloseHandle(pipe_out[1]);
  if (ret == 0) {
    return false;
  }
  int fd = _open_osfhandle((intptr_t)pipe_out[0], O_BINARY);
  const auto compiler_check_result = hash.hash_fd(fd);
  if (!compiler_check_result) {
    LOG("Error hashing compiler check command output: {}",
        compiler_check_result.error());
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pipe_out[0]);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitcode != 0) {
    LOG("Compiler check command returned {}", exitcode);
    return false;
  }
  return bool(compiler_check_result);
#else
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    throw core::Fatal(FMT("pipe failed: {}", strerror(errno)));
  }

  pid_t pid = fork();
  if (pid == -1) {
    throw core::Fatal(FMT("fork failed: {}", strerror(errno)));
  }

  if (pid == 0) {
    // Child.
    close(pipefd[0]);
    close(0);
    dup2(pipefd[1], 1);
    dup2(pipefd[1], 2);
    _exit(execvp(argv[0], const_cast<char* const*>(argv.data())));
    // Never reached.
  } else {
    // Parent.
    close(pipefd[1]);
    const auto hash_result = hash.hash_fd(pipefd[0]);
    if (!hash_result) {
      LOG("Error hashing compiler check command output: {}",
          hash_result.error());
    }
    close(pipefd[0]);

    int status;
    int result;
    while ((result = waitpid(pid, &status, 0)) != pid) {
      if (result == -1 && errno == EINTR) {
        continue;
      }
      LOG("waitpid failed: {}", strerror(errno));
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      LOG("Compiler check command returned {}", WEXITSTATUS(status));
      return false;
    }
    return bool(hash_result);
  }
#endif
}

bool
hash_multicommand_output(Hash& hash,
                         const std::string& command,
                         const std::string& compiler)
{
  for (const std::string& cmd : util::split_into_strings(command, ";")) {
    if (!hash_command_output(hash, cmd, compiler)) {
      return false;
    }
  }
  return true;
}
