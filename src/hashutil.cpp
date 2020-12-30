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

#include "hashutil.hpp"

#include "Args.hpp"
#include "Context.hpp"
#include "Hash.hpp"
#include "core/Config.hpp"
#include "core/Logging.hpp"
#include "core/Sloppy.hpp"
#include "core/Stat.hpp"
#include "core/fmtmacros.hpp"
#include "execute.hpp"
#include "macroskip.hpp"

#include "third_party/blake3/blake3_cpu_supports_avx2.h"

#ifdef INODE_CACHE_SUPPORTED
#  include "InodeCache.hpp"
#endif

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#ifdef HAVE_AVX2
#  include <immintrin.h>
#endif

using nonstd::string_view;

namespace {

// Returns one of HASH_SOURCE_CODE_FOUND_DATE, HASH_SOURCE_CODE_FOUND_TIME or
// HASH_SOURCE_CODE_FOUND_TIMESTAMP if "_DATE__", "_TIME__" or "_TIMESTAMP__"
// starts at str[pos].
//
// Pre-condition: str[pos - 1] == '_'
int
check_for_temporal_macros_helper(string_view str, size_t pos)
{
  if (pos + 7 > str.length()) {
    return 0;
  }

  int found = 0;
  int macro_len = 7;
  if (memcmp(&str[pos], "_DATE__", 7) == 0) {
    found = HASH_SOURCE_CODE_FOUND_DATE;
  } else if (memcmp(&str[pos], "_TIME__", 7) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIME;
  } else if (pos + 12 <= str.length()
             && memcmp(&str[pos], "_TIMESTAMP__", 12) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIMESTAMP;
    macro_len = 12;
  } else {
    return 0;
  }

  // Check char before and after macro to verify that the found macro isn't part
  // of another identifier.
  if ((pos == 1 || (str[pos - 2] != '_' && !isalnum(str[pos - 2])))
      && (pos + macro_len == str.length()
          || (str[pos + macro_len] != '_' && !isalnum(str[pos + macro_len])))) {
    return found;
  }

  return 0;
}

int
check_for_temporal_macros_bmh(string_view str)
{
  int result = 0;

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  size_t i = 7;

  while (i < str.length()) {
    // Check whether the substring ending at str[i] has the form "_....E..". On
    // the assumption that 'E' is less common in source than '_', we check
    // str[i-2] first.
    if (str[i - 2] == 'E' && str[i - 7] == '_') {
      result |= check_for_temporal_macros_helper(str, i - 6);
    }

    // macro_skip tells us how far we can skip forward upon seeing str[i] at
    // the end of a substring.
    i += macro_skip[(uint8_t)str[i]];
  }

  return result;
}

#ifdef HAVE_AVX2
int check_for_temporal_macros_avx2(string_view str)
  __attribute__((target("avx2")));

// The following algorithm, which uses AVX2 instructions to find __DATE__,
// __TIME__ and __TIMESTAMP__, is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
int
check_for_temporal_macros_avx2(string_view str)
{
  int result = 0;

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
      const auto start = pos + __builtin_ctz(mask) + 1;

      // Clear the least significant bit set.
      mask = mask & (mask - 1);

      result |= check_for_temporal_macros_helper(str, start);
    }
  }

  result |= check_for_temporal_macros_bmh(str.substr(pos));

  return result;
}
#endif

int
hash_source_code_file_nocache(const Context& ctx,
                              Hash& hash,
                              const std::string& path,
                              size_t size_hint,
                              bool is_precompiled)
{
  if (is_precompiled) {
    if (hash.hash_file(path)) {
      return HASH_SOURCE_CODE_OK;
    } else {
      return HASH_SOURCE_CODE_ERROR;
    }
  } else {
    std::string data;
    try {
      data = Util::read_file(path, size_hint);
    } catch (Error&) {
      return HASH_SOURCE_CODE_ERROR;
    }
    int result = hash_source_code_string(ctx, hash, data, path);
    return result;
  }
}

#ifdef INODE_CACHE_SUPPORTED
InodeCache::ContentType
get_content_type(const Config& config, const std::string& path)
{
  if (Util::is_precompiled_header(path)) {
    return InodeCache::ContentType::precompiled_header;
  }
  if (config.sloppiness() & SLOPPY_TIME_MACROS) {
    return InodeCache::ContentType::code_with_sloppy_time_macros;
  }
  return InodeCache::ContentType::code;
}
#endif

} // namespace

int
check_for_temporal_macros(string_view str)
{
#ifdef HAVE_AVX2
  if (blake3_cpu_supports_avx2()) {
    return check_for_temporal_macros_avx2(str);
  }
#endif
  return check_for_temporal_macros_bmh(str);
}

int
hash_source_code_string(const Context& ctx,
                        Hash& hash,
                        string_view str,
                        const std::string& path)
{
  int result = HASH_SOURCE_CODE_OK;

  // Check for __DATE__, __TIME__ and __TIMESTAMP__if the sloppiness
  // configuration tells us we should.
  if (!(ctx.config.sloppiness() & SLOPPY_TIME_MACROS)) {
    result |= check_for_temporal_macros(str);
  }

  // Hash the source string.
  hash.hash(str);

  if (result & HASH_SOURCE_CODE_FOUND_DATE) {
    LOG("Found __DATE__ in {}", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __DATE__ changes.
    hash.hash_delimiter("date");
    auto now = Util::localtime();
    if (!now) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash.hash(now->tm_year);
    hash.hash(now->tm_mon);
    hash.hash(now->tm_mday);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIME) {
    // We don't know for sure that the program actually uses the __TIME__ macro,
    // but we have to assume it anyway and hash the time stamp. However, that's
    // not very useful since the chance that we get a cache hit later the same
    // second should be quite slim... So, just signal back to the caller that
    // __TIME__ has been found so that the direct mode can be disabled.
    LOG("Found __TIME__ in {}", path);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIMESTAMP) {
    LOG("Found __TIMESTAMP__ in {}", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __TIMESTAMP__ changes.
    const auto stat = Stat::stat(path);
    if (!stat) {
      return HASH_SOURCE_CODE_ERROR;
    }

    auto modified_time = Util::localtime(stat.mtime());
    if (!modified_time) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash.hash_delimiter("timestamp");
#ifdef HAVE_ASCTIME_R
    char buffer[26];
    auto timestamp = asctime_r(&*modified_time, buffer);
#else
    auto timestamp = asctime(&*modified_time);
#endif
    if (!timestamp) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash.hash(timestamp);
  }

  return result;
}

int
hash_source_code_file(const Context& ctx,
                      Hash& hash,
                      const std::string& path,
                      size_t size_hint)
{
#ifdef INODE_CACHE_SUPPORTED
  if (!ctx.config.inode_cache()) {
#endif
    return hash_source_code_file_nocache(
      ctx, hash, path, size_hint, Util::is_precompiled_header(path));

#ifdef INODE_CACHE_SUPPORTED
  }

  // Reusable file hashes must be independent of the outer context. Thus hash
  // files separately so that digests based on file contents can be reused. Then
  // add the digest into the outer hash instead.
  InodeCache::ContentType content_type = get_content_type(ctx.config, path);
  Digest digest;
  int return_value;
  if (!ctx.inode_cache.get(path, content_type, digest, &return_value)) {
    Hash file_hash;
    return_value = hash_source_code_file_nocache(
      ctx,
      file_hash,
      path,
      size_hint,
      content_type == InodeCache::ContentType::precompiled_header);
    if (return_value == HASH_SOURCE_CODE_ERROR) {
      return HASH_SOURCE_CODE_ERROR;
    }
    digest = file_hash.digest();
    ctx.inode_cache.put(path, content_type, digest, return_value);
  }
  hash.hash(digest.bytes(), Digest::size(), Hash::HashType::binary);
  return return_value;
#endif
}

bool
hash_binary_file(const Context& ctx, Hash& hash, const std::string& path)
{
  if (!ctx.config.inode_cache()) {
    return hash.hash_file(path);
  }

#ifdef INODE_CACHE_SUPPORTED
  // Reusable file hashes must be independent of the outer context. Thus hash
  // files separately so that digests based on file contents can be reused. Then
  // add the digest into the outer hash instead.
  Digest digest;
  if (!ctx.inode_cache.get(path, InodeCache::ContentType::binary, digest)) {
    Hash file_hash;
    if (!file_hash.hash_file(path)) {
      return false;
    }
    digest = file_hash.digest();
    ctx.inode_cache.put(path, InodeCache::ContentType::binary, digest);
  }
  hash.hash(digest.bytes(), Digest::size(), Hash::HashType::binary);
  return true;
#else
  return hash.hash_file(path);
#endif
}

bool
hash_command_output(Hash& hash,
                    const std::string& command,
                    const std::string& compiler)
{
#ifdef _WIN32
  std::string adjusted_command = Util::strip_whitespace(command);

  // Add "echo" command.
  bool using_cmd_exe;
  if (Util::starts_with(adjusted_command, "echo")) {
    adjusted_command = FMT("cmd.exe /c \"{}\"", adjusted_command);
    using_cmd_exe = true;
  } else if (Util::starts_with(adjusted_command, "%compiler%")
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
      Util::format_argv_for_logging(argv.data()));

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  std::string path = find_executable_in_path(args[0], "", getenv("PATH"));
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
    win32args = Win32Util::argv_to_string(argv.data(), sh);
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
  bool ok = hash.hash_fd(fd);
  if (!ok) {
    LOG("Error hashing compiler check command output: {}", strerror(errno));
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
  return ok;
#else
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    throw Fatal("pipe failed: {}", strerror(errno));
  }

  pid_t pid = fork();
  if (pid == -1) {
    throw Fatal("fork failed: {}", strerror(errno));
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
    bool ok = hash.hash_fd(pipefd[0]);
    if (!ok) {
      LOG("Error hashing compiler check command output: {}", strerror(errno));
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
    return ok;
  }
#endif
}

bool
hash_multicommand_output(Hash& hash,
                         const std::string& command,
                         const std::string& compiler)
{
  for (const std::string& cmd : Util::split_into_strings(command, ";")) {
    if (!hash_command_output(hash, cmd, compiler)) {
      return false;
    }
  }
  return true;
}
