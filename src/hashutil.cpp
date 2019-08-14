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

#include "hashutil.hpp"

#include "ccache.hpp"
#include "macroskip.hpp"

#include "third_party/xxhash.h"

unsigned
hash_from_string(void* str)
{
  return XXH64(str, strlen((const char*)str), 0);
}

unsigned
hash_from_int(int i)
{
  return XXH64(&i, sizeof(int), 0);
}

int
strings_equal(void* str1, void* str2)
{
  return str_eq((const char*)str1, (const char*)str2);
}

// Search for the strings "__DATE__" and "__TIME__" in str.
//
// Returns a bitmask with HASH_SOURCE_CODE_FOUND_DATE and
// HASH_SOURCE_CODE_FOUND_TIME set appropriately.
int
check_for_temporal_macros(const char* str, size_t len)
{
  int result = 0;

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  size_t i = 7;

  while (i < len) {
    // Check whether the substring ending at str[i] has the form "__...E__". On
    // the assumption that 'E' is less common in source than '_', we check
    // str[i-2] first.
    if (str[i - 2] == 'E' && str[i - 0] == '_' && str[i - 7] == '_'
        && str[i - 1] == '_' && str[i - 6] == '_'
        && (i < 8 || (str[i - 8] != '_' && !isalnum(str[i - 8])))
        && (i + 1 >= len || (str[i + 1] != '_' && !isalnum(str[i + 1])))) {
      // Check the remaining characters to see if the substring is "__DATE__"
      // or "__TIME__".
      if (str[i - 5] == 'D' && str[i - 4] == 'A' && str[i - 3] == 'T') {
        result |= HASH_SOURCE_CODE_FOUND_DATE;
      } else if (str[i - 5] == 'T' && str[i - 4] == 'I' && str[i - 3] == 'M') {
        result |= HASH_SOURCE_CODE_FOUND_TIME;
      }
    }

    // macro_skip tells us how far we can skip forward upon seeing str[i] at
    // the end of a substring.
    i += macro_skip[(uint8_t)str[i]];
  }

  return result;
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

  // Check for __DATE__ and __TIME__ if the sloppiness configuration tells us
  // we should.
  if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
    result |= check_for_temporal_macros(str, len);
  }

  // Hash the source string.
  hash_string_buffer(hash, str, len);

  if (result & HASH_SOURCE_CODE_FOUND_DATE) {
    // Make sure that the hash sum changes if the (potential) expansion of
    // __DATE__ changes.
    time_t t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);
    cc_log("Found __DATE__ in %s", path);
    hash_delimiter(hash, "date");
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

  return result;
}

// Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
// results.
int
hash_source_code_file(const Config& config, struct hash* hash, const char* path)
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
    if (!read_file(path, 0, &data, &size)) {
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

  struct args* args = args_init_from_string(command);
  for (int i = 0; i < args->argc; i++) {
    if (str_eq(args->argv[i], "%compiler%")) {
      args_set(args, i, compiler);
    }
  }
  cc_log_argv("Executing compiler check command ", args->argv);

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  char* path = find_executable(args->argv[0], NULL);
  if (!path) {
    path = args->argv[0];
  }
  char* sh = win32getshell(path);
  if (sh) {
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
    win32args = win32argvtos(sh, args->argv, &length);
  } else {
    win32args = (char*)command; // quoted
  }
  BOOL ret =
    CreateProcess(path, win32args, NULL, NULL, 1, 0, NULL, NULL, &si, &pi);
  CloseHandle(pipe_out[1]);
  args_free(args);
  free(win32args);
  if (!cmd) {
    free((char*)command); // Original argument was replaced above.
  }
  if (ret == 0) {
    stats_update(STATS_COMPCHECK);
    return false;
  }
  int fd = _open_osfhandle((intptr_t)pipe_out[0], O_BINARY);
  bool ok = hash_fd(hash, fd);
  if (!ok) {
    cc_log("Error hashing compiler check command output: %s", strerror(errno));
    stats_update(STATS_COMPCHECK);
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pipe_out[0]);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitcode != 0) {
    cc_log("Compiler check command returned %d", (int)exitcode);
    stats_update(STATS_COMPCHECK);
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
    _exit(execvp(args->argv[0], args->argv));
    // Never reached.
  } else {
    // Parent.
    args_free(args);
    close(pipefd[1]);
    bool ok = hash_fd(hash, pipefd[0]);
    if (!ok) {
      cc_log("Error hashing compiler check command output: %s",
             strerror(errno));
      stats_update(STATS_COMPCHECK);
    }
    close(pipefd[0]);

    int status;
    if (waitpid(pid, &status, 0) != pid) {
      cc_log("waitpid failed");
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      cc_log("Compiler check command returned %d", WEXITSTATUS(status));
      stats_update(STATS_COMPCHECK);
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
  char* command_string = x_strdup(commands);
  char* p = command_string;
  char* command;
  char* saveptr = NULL;
  bool ok = true;
  while ((command = strtok_r(p, ";", &saveptr))) {
    if (!hash_command_output(hash, command, compiler)) {
      ok = false;
    }
    p = NULL;
  }
  free(command_string);
  return ok;
}
