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
#include "Config.hpp"
#include "Context.hpp"
#include "Stat.hpp"
#include "ccache.hpp"
#include "execute.hpp"
#include "logging.hpp"
#include "macroskip.hpp"
#include "stats.hpp"

#include "third_party/xxhash.h"

// With older GCC (libgcc), __builtin_cpu_supports("avx2) returns true if AVX2
// is supported by the CPU but disabled by the OS. This was fixed in GCC 8, 7.4
// and 6.5 (see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85100).
//
// For Clang it seems to be correct if compiler-rt is used as -rtlib, at least
// as of 3.9 (see https://bugs.llvm.org/show_bug.cgi?id=25510). But if libgcc
// is used we have the same problem as mentioned above. Unfortunately there
// doesn't seem to be a way to detect which one is used, or the version of
// libgcc when used by Clang, so assume that it works with Clang >= 3.9.
#if !(__GNUC__ >= 8 || (__GNUC__ == 7 && __GNUC_MINOR__ >= 4)                  \
      || (__GNUC__ == 6 && __GNUC_MINOR__ >= 5) || __clang_major__ > 3         \
      || (__clang_major__ == 3 && __clang_minor__ >= 9))
#  undef HAVE_AVX2
#endif

#ifdef HAVE_AVX2
#  include <immintrin.h>
#endif

unsigned
hash_from_int(int i)
{
  return XXH64(&i, sizeof(int), 0);
}

// Returns one of HASH_SOURCE_CODE_FOUND_DATE, HASH_SOURCE_CODE_FOUND_TIME or
// HASH_SOURCE_CODE_FOUND_TIMESTAMP if "_DATE__", "_TIME__" or "_TIMESTAMP__"
// starts at str[pos].
//
// Pre-condition: str[pos - 1] == '_'
static int
check_for_temporal_macros_helper(const char* str, size_t len, size_t pos)
{
  if (pos + 7 > len) {
    return 0;
  }

  int found = 0;
  int macro_len = 7;
  if (memcmp(str + pos, "_DATE__", 7) == 0) {
    found = HASH_SOURCE_CODE_FOUND_DATE;
  } else if (memcmp(str + pos, "_TIME__", 7) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIME;
  } else if (pos + 12 <= len && memcmp(str + pos, "_TIMESTAMP__", 12) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIMESTAMP;
    macro_len = 12;
  } else {
    return 0;
  }

  // Check char before and after macro to verify that the found macro isn't
  // part of another identifier.
  if ((pos == 1 || (str[pos - 2] != '_' && !isalnum(str[pos - 2])))
      && (pos + macro_len == len
          || (str[pos + macro_len] != '_' && !isalnum(str[pos + macro_len])))) {
    return found;
  }

  return 0;
}

static int
check_for_temporal_macros_bmh(const char* str, size_t len)
{
  int result = 0;

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  size_t i = 7;

  while (i < len) {
    // Check whether the substring ending at str[i] has the form "_....E..". On
    // the assumption that 'E' is less common in source than '_', we check
    // str[i-2] first.
    if (str[i - 2] == 'E' && str[i - 7] == '_') {
      result |= check_for_temporal_macros_helper(str, len, i - 6);
    }

    // macro_skip tells us how far we can skip forward upon seeing str[i] at
    // the end of a substring.
    i += macro_skip[(uint8_t)str[i]];
  }

  return result;
}

#ifdef HAVE_AVX2
static int check_for_temporal_macros_avx2(const char* str, size_t len)
  __attribute__((target("avx2")));

// The following algorithm, which uses AVX2 instructions to find __DATE__,
// __TIME__ and __TIMESTAMP__, is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
static int
check_for_temporal_macros_avx2(const char* str, size_t len)
{
  int result = 0;

  // Set all 32 bytes in first and last to '_' and 'E' respectively.
  const __m256i first = _mm256_set1_epi8('_');
  const __m256i last = _mm256_set1_epi8('E');

  size_t pos = 0;
  for (; pos + 5 + 32 <= len; pos += 32) {
    // Load 32 bytes from the current position in the input string, with
    // block_last being offset 5 bytes (i.e. the offset of 'E' in all three
    // macros).
    const __m256i block_first =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str + pos));
    const __m256i block_last =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str + pos + 5));

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

      result |= check_for_temporal_macros_helper(str, len, start);
    }
  }

  result |= check_for_temporal_macros_bmh(str + pos, len - pos);

  return result;
}
#endif

// Search for the strings "__DATE__", "__TIME__" and "__TIMESTAMP__" in str.
//
// Returns a bitmask with HASH_SOURCE_CODE_FOUND_DATE,
// HASH_SOURCE_CODE_FOUND_TIME and HASH_SOURCE_CODE_FOUND_TIMESTAMP set
// appropriately.
int
check_for_temporal_macros(const char* str, size_t len)
{
#ifdef HAVE_AVX2
  if (__builtin_cpu_supports("avx2")) {
    return check_for_temporal_macros_avx2(str, len);
  }
#endif
  return check_for_temporal_macros_bmh(str, len);
}

// Hash a string. Returns a bitmask of HASH_SOURCE_CODE_* results.
int
hash_source_code_string(const Config& config,
                        struct hash* hash,
                        const char* str,
                        size_t len,
                        const char* path)
{
  int result = HASH_SOURCE_CODE_OK;

  // Check for __DATE__, __TIME__ and __TIMESTAMP__if the sloppiness
  // configuration tells us we should.
  if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
    result |= check_for_temporal_macros(str, len);
  }

  // Hash the source string.
  hash_string_buffer(hash, str, len);

  if (result & HASH_SOURCE_CODE_FOUND_DATE) {
    cc_log("Found __DATE__ in %s", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __DATE__ changes.
    time_t t = time(nullptr);
    struct tm now;
    hash_delimiter(hash, "date");
    if (!localtime_r(&t, &now)) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash_int(hash, now.tm_year);
    hash_int(hash, now.tm_mon);
    hash_int(hash, now.tm_mday);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIME) {
    // We don't know for sure that the program actually uses the __TIME__
    // macro, but we have to assume it anyway and hash the time stamp. However,
    // that's not very useful since the chance that we get a cache hit later
    // the same second should be quite slim... So, just signal back to the
    // caller that __TIME__ has been found so that the direct mode can be
    // disabled.
    cc_log("Found __TIME__ in %s", path);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIMESTAMP) {
    cc_log("Found __TIMESTAMP__ in %s", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __TIMESTAMP__ changes.
    const auto stat = Stat::stat(path);
    if (!stat) {
      return HASH_SOURCE_CODE_ERROR;
    }

    time_t t = stat.mtime();
    tm modified;
    hash_delimiter(hash, "timestamp");
    if (!localtime_r(&t, &modified)) {
      return HASH_SOURCE_CODE_ERROR;
    }

#ifdef HAVE_ASCTIME_R
    char buffer[26];
    auto timestamp = asctime_r(&modified, buffer);
#else
    auto timestamp = asctime(&modified);
#endif
    if (!timestamp) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash_string(hash, timestamp);
  }

  return result;
}

// Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
// results.
int
hash_source_code_file(const Config& config,
                      struct hash* hash,
                      const char* path,
                      size_t size_hint)
{
  if (is_precompiled_header(path)) {
    if (hash_file(hash, path)) {
      return HASH_SOURCE_CODE_OK;
    } else {
      return HASH_SOURCE_CODE_ERROR;
    }
  } else {
    char* data;
    size_t size;
    if (!read_file(path, size_hint, &data, &size)) {
      return HASH_SOURCE_CODE_ERROR;
    }
    int result = hash_source_code_string(config, hash, data, size, path);
    free(data);
    return result;
  }
}

bool
hash_command_output(struct hash* hash,
                    const char* command,
                    const char* compiler)
{
#ifdef _WIN32
  // Trim leading space.
  while (isspace(*command)) {
    command++;
  }

  // Add "echo" command.
  bool cmd;
  if (str_startswith(command, "echo")) {
    command = format("cmd.exe /c \"%s\"", command);
    cmd = true;
  } else if (str_startswith(command, "%compiler%")
             && str_eq(compiler, "echo")) {
    command = format("cmd.exe /c \"%s%s\"", compiler, command + 10);
    cmd = true;
  } else {
    command = x_strdup(command);
    cmd = false;
  }
#endif

  Args args = Args::from_string(command);

  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "%compiler%") {
      args[i] = compiler;
    }
  }

  auto argv = args.to_argv();
  cc_log_argv("Executing compiler check command ", argv.data());

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  std::string path =
    find_executable_in_path(args[0].c_str(), nullptr, getenv("PATH"));
  if (path.empty()) {
    path = args[0];
  }
  std::string sh = win32getshell(path.c_str());
  if (!sh.empty()) {
    path = sh;
  }

  si.cb = sizeof(STARTUPINFO);

  HANDLE pipe_out[2];
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
  CreatePipe(&pipe_out[0], &pipe_out[1], &sa, 0);
  SetHandleInformation(pipe_out[0], HANDLE_FLAG_INHERIT, 0);
  si.hStdOutput = pipe_out[1];
  si.hStdError = pipe_out[1];
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  char* win32args;
  if (!cmd) {
    int length;
    const char* prefix = sh.empty() ? nullptr : sh.c_str();
    win32args = win32argvtos(prefix, argv.data(), &length);
  } else {
    win32args = (char*)command; // quoted
  }
  BOOL ret = CreateProcess(
    path.c_str(), win32args, NULL, NULL, 1, 0, NULL, NULL, &si, &pi);
  CloseHandle(pipe_out[1]);
  free(win32args);
  if (!cmd) {
    free((char*)command); // Original argument was replaced above.
  }
  if (ret == 0) {
    return false;
  }
  int fd = _open_osfhandle((intptr_t)pipe_out[0], O_BINARY);
  bool ok = hash_fd(hash, fd);
  if (!ok) {
    cc_log("Error hashing compiler check command output: %s", strerror(errno));
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pipe_out[0]);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitcode != 0) {
    cc_log("Compiler check command returned %d", (int)exitcode);
    return false;
  }
  return ok;
#else
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    fatal("pipe failed");
  }

  pid_t pid = fork();
  if (pid == -1) {
    fatal("fork failed");
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
    bool ok = hash_fd(hash, pipefd[0]);
    if (!ok) {
      cc_log("Error hashing compiler check command output: %s",
             strerror(errno));
    }
    close(pipefd[0]);

    int status;
    if (waitpid(pid, &status, 0) != pid) {
      cc_log("waitpid failed");
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      cc_log("Compiler check command returned %d", WEXITSTATUS(status));
      return false;
    }
    return ok;
  }
#endif
}

bool
hash_multicommand_output(struct hash* hash,
                         const char* commands,
                         const char* compiler)
{
  bool ok = true;
  for (const std::string& cmd : Util::split_into_strings(commands, ";")) {
    if (!hash_command_output(hash, cmd.c_str(), compiler)) {
      ok = false;
    }
  }
  return ok;
}
