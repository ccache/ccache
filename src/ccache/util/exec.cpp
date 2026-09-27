// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "exec.hpp"

#include <ccache/util/environment.hpp>
#include <ccache/util/error.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/wincompat.hpp>

#include <optional>

#ifndef _WIN32
#  include <spawn.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#ifndef environ
DLLIMPORT extern char** environ;
#endif

namespace fs = util::filesystem;

// Call a function that returns 0 on success and either an error code or -1 (and
// sets errno) on failure.
#define CHECK_LIB_CALL(function, ...)                                          \
  do {                                                                         \
    int _result = function(__VA_ARGS__);                                       \
    if (_result != 0) {                                                        \
      return tl::unexpected(FMT(#function " failed: {}",                       \
                                strerror(_result == -1 ? errno : _result)));   \
    }                                                                          \
  } while (false)

namespace util {

namespace {

tl::expected<std::string, std::string>
exec_to_string_impl(const Args& args,
                    const std::optional<std::string_view> standard_input)
{
  auto argv = args.to_argv();
  LOG("Executing command: {}", format_argv_for_logging(argv.data()));

  std::string output;

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  si.cb = sizeof(STARTUPINFO);

  HANDLE output_read_handle = nullptr;
  HANDLE output_write_handle = nullptr;
  HANDLE input_read_handle = nullptr;
  HANDLE input_write_handle = nullptr;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  if (!CreatePipe(&output_read_handle, &output_write_handle, &sa, 0)
      || !SetHandleInformation(output_read_handle, HANDLE_FLAG_INHERIT, 0)
      || (standard_input
          && (!CreatePipe(&input_read_handle, &input_write_handle, &sa, 0)
              || !SetHandleInformation(input_write_handle, HANDLE_FLAG_INHERIT, 0)))) {
    return tl::unexpected(FMT("CreatePipe failure: {}", win32_error_message(GetLastError())));
  }
  si.hStdOutput = output_write_handle;
  si.hStdError = output_write_handle;
  si.hStdInput = standard_input ? input_read_handle : nullptr;
  si.dwFlags = STARTF_USESTDHANDLES;

  std::string commandline = format_argv_as_win32_command_string(argv.data());
  BOOL ret = CreateProcess(nullptr,
                           const_cast<char*>(commandline.c_str()),
                           nullptr,
                           nullptr,
                           1, // inherit handles
                           0, // no console window
                           nullptr,
                           nullptr,
                           &si,
                           &pi);
  CloseHandle(output_write_handle);
  if (standard_input) {
    CloseHandle(input_read_handle);
  }
  if (ret == 0) {
    CloseHandle(output_read_handle);
    if (standard_input) {
      CloseHandle(input_write_handle);
    }
    DWORD error = GetLastError();
    return tl::unexpected(
      FMT("CreateProcess failure: {} ({})", win32_error_message(error), error));
  }
  if (standard_input) {
    size_t input_offset = 0;
    while (input_offset < standard_input->size()) {
      DWORD bytes_written = 0;
      if (!WriteFile(input_write_handle,
                     standard_input->data() + input_offset,
                     static_cast<DWORD>(standard_input->size() - input_offset),
                     &bytes_written,
                     nullptr)) {
        CloseHandle(input_write_handle);
        CloseHandle(output_read_handle);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return tl::unexpected(
          FMT("WriteFile failure: {}", win32_error_message(GetLastError())));
      }
      input_offset += bytes_written;
    }
    CloseHandle(input_write_handle);
  }
  char buffer[4096];
  DWORD bytes_read = 0;
  while (ReadFile(output_read_handle, buffer, sizeof(buffer), &bytes_read, nullptr)
         && bytes_read > 0) {
    output.append(buffer, bytes_read);
  }

  CloseHandle(output_read_handle);
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitcode != 0) {
    return tl::unexpected(FMT("Non-zero exit code: {}", exitcode));
  }
#else
  int output_pipefd[2];
  int input_pipefd[2];
  CHECK_LIB_CALL(pipe, output_pipefd);
  if (standard_input) {
    CHECK_LIB_CALL(pipe, input_pipefd);
  }

  posix_spawn_file_actions_t fa;
  CHECK_LIB_CALL(posix_spawn_file_actions_init, &fa);
  CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &fa, output_pipefd[0]);
  CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &fa, 0);
  if (standard_input) {
    CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &fa, input_pipefd[1]);
    CHECK_LIB_CALL(posix_spawn_file_actions_adddup2, &fa, input_pipefd[0], 0);
  }
  CHECK_LIB_CALL(posix_spawn_file_actions_adddup2, &fa, output_pipefd[1], 1);
  CHECK_LIB_CALL(posix_spawn_file_actions_adddup2, &fa, output_pipefd[1], 2);

  pid_t pid;
  auto argv_mutable = const_cast<char* const*>(argv.data());
  int spawn_result =
    posix_spawnp(&pid, argv[0], &fa, nullptr, argv_mutable, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(output_pipefd[1]);
  if (standard_input) {
    close(input_pipefd[0]);
  }

  if (spawn_result != 0) {
    close(output_pipefd[0]);
    if (standard_input) {
      close(input_pipefd[1]);
    }
    return tl::unexpected(
      FMT("posix_spawnp failed: {}", strerror(spawn_result)));
  }

  if (standard_input) {
    size_t input_offset = 0;
    while (input_offset < standard_input->size()) {
      const auto written = write(input_pipefd[1],
                                 standard_input->data() + input_offset,
                                 standard_input->size() - input_offset);
      if (written <= 0) {
        close(input_pipefd[1]);
        close(output_pipefd[0]);
        return tl::unexpected(
          written == 0 ? "write failed: no bytes written"
                       : FMT("write failed: {}", strerror(errno)));
      }
      input_offset += static_cast<size_t>(written);
    }
    close(input_pipefd[1]);
  }

  auto read_result = read_fd(output_pipefd[0], [&](auto data) {
    output.append(reinterpret_cast<const char*>(data.data()), data.size());
  });

  int status;
  while (waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR) {
      return tl::unexpected(FMT("waitpid failed: {}", strerror(errno)));
    }
  }
  if (!read_result) {
    return tl::unexpected(FMT("failed to read pipe: {}", read_result.error()));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return tl::unexpected(FMT("Non-zero exit code: {}", WEXITSTATUS(status)));
  }
#endif

  return output;
}

} // namespace

tl::expected<std::string, std::string>
exec_to_string(const Args& args)
{
  return exec_to_string_impl(args, std::nullopt);
}

tl::expected<std::string, std::string>
exec_credential_helper_to_string(const Args& args,
                                 const std::string_view standard_input)
{
  return exec_to_string_impl(args, standard_input);
}

} // namespace util
