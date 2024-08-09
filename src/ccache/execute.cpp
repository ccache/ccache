// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2011-2024 Joel Rosdahl and other contributors
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

#include "execute.hpp"

#include <ccache/ccache.hpp>
#include <ccache/config.hpp>
#include <ccache/context.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/signalhandler.hpp>
#include <ccache/util/defer.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/error.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/temporaryfile.hpp>
#include <ccache/util/wincompat.hpp>

#include <vector>

#ifdef HAVE_SPAWN_H
#  include <spawn.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

namespace fs = util::filesystem;

#ifdef _WIN32
static int win32execute(const char* path,
                        const char* const* argv,
                        int doreturn,
                        int fd_stdout,
                        int fd_stderr,
                        const std::string& temp_dir);

int
execute(Context& ctx,
        const char* const* argv,
        util::Fd&& fd_out,
        util::Fd&& fd_err)
{
  LOG("Executing {}", util::format_argv_for_logging(argv));

  return win32execute(argv[0],
                      argv,
                      1,
                      fd_out.release(),
                      fd_err.release(),
                      util::pstr(ctx.config.temporary_dir()));
}

void
execute_noreturn(const char* const* argv, const fs::path& temp_dir)
{
  win32execute(argv[0], argv, 0, -1, -1, util::pstr(temp_dir).c_str());
}

static const std::u16string
getsh()
{
  auto path_list = get_PATH();
  sh = util::pstr(find_executable_in_path("sh.exe", path_list));
  if (sh.empty()) {
    sh = util::pstr(find_executable_in_path("bash.exe", path_list));
  }
  return sh;
}

const std::u16string
win32getshell(const std::u16string& path)
{
  auto path_list = get_PATH();
  std::u16string sh;
  if (util::to_lowercase(util::pstr(fs::path(path).extension()).str()) == ".sh"
      && path_list) {
    sh = getsh();
  }
  if (sh.empty() && getenv("CCACHE_DETECT_SHEBANG")) {
    // Detect shebang.
    util::FileStream fp(path, "r");
    if (fp) {
      char buf[10] = {0};
      fgets(buf, sizeof(buf) - 1, fp.get());
      if (std::string(buf) == "#!/bin/sh" && path_list) {
        sh = getsh();
      }
    }
  }

  return sh;
}

int
win32execute(const char* path,
             const char* const* argv,
             int doreturn,
             int fd_stdout,
             int fd_stderr,
             const std::string& temp_dir)
{
  BOOL is_process_in_job = false;
  DWORD dw_creation_flags = 0;

  {
    BOOL job_success =
      IsProcessInJob(GetCurrentProcess(), nullptr, &is_process_in_job);
    if (!job_success) {
      DWORD error = GetLastError();
      LOG("failed to IsProcessInJob: {} ({})",
          util::win32_error_message(error),
          error);
      return 0;
    }
    if (is_process_in_job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
      BOOL querySuccess =
        QueryInformationJobObject(nullptr,
                                  JobObjectExtendedLimitInformation,
                                  &jobInfo,
                                  sizeof(jobInfo),
                                  nullptr);
      if (!querySuccess) {
        DWORD error = GetLastError();
        LOG("failed to QueryInformationJobObject: {} ({})",
            util::win32_error_message(error),
            error);
        return 0;
      }

      const auto& limit_flags = jobInfo.BasicLimitInformation.LimitFlags;
      bool is_kill_active = limit_flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      bool allow_break_away = limit_flags & JOB_OBJECT_LIMIT_BREAKAWAY_OK;
      if (!is_kill_active && allow_break_away) {
        is_process_in_job = false;
        dw_creation_flags = CREATE_BREAKAWAY_FROM_JOB | CREATE_SUSPENDED;
      }
    } else {
      dw_creation_flags = CREATE_SUSPENDED;
    }
  }

  HANDLE job = nullptr;
  if (!is_process_in_job) {
    job = CreateJobObject(nullptr, nullptr);
    if (job == nullptr) {
      DWORD error = GetLastError();
      LOG("failed to CreateJobObject: {} ({})",
          util::win32_error_message(error),
          error);
      return -1;
    }

    {
      // Set the job object to terminate all child processes when the parent
      // process is killed.
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
      jobInfo.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
      BOOL job_success = SetInformationJobObject(
        job, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));
      if (!job_success) {
        DWORD error = GetLastError();
        LOG("failed to JobObjectExtendedLimitInformation: {} ({})",
            util::win32_error_message(error),
            error);
        return -1;
      }
    }
  }

  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));

  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  std::string sh = win32getshell(path);
  if (!sh.empty()) {
    path = sh.c_str();
  }

  si.cb = sizeof(STARTUPINFO);
  if (fd_stdout != -1) {
    si.hStdOutput = (HANDLE)_get_osfhandle(fd_stdout);
    si.hStdError = (HANDLE)_get_osfhandle(fd_stderr);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;
    if (si.hStdOutput == INVALID_HANDLE_VALUE
        || si.hStdError == INVALID_HANDLE_VALUE) {
      return -1;
    }
  } else {
    // Redirect subprocess stdout, stderr into current process.
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;
    if (si.hStdOutput == INVALID_HANDLE_VALUE
        || si.hStdError == INVALID_HANDLE_VALUE) {
      return -1;
    }
  }

  std::string args = util::format_argv_as_win32_command_string(argv, sh);
  std::string full_path = util::add_exe_suffix(path);
  fs::path tmp_file_path;

  DEFER([&] {
    if (!tmp_file_path.empty()) {
      util::remove(tmp_file_path);
    }
  });

  if (args.length() > 8192) {
    auto tmp_file = util::value_or_throw<core::Fatal>(
      util::TemporaryFile::create(FMT("{}/cmd_args", temp_dir)));
    args = util::format_argv_as_win32_command_string(argv + 1, sh, true);
    util::write_fd(*tmp_file.fd, args.data(), args.length());
    args = FMT(R"("{}" "@{}")", full_path, tmp_file.path);
    tmp_file_path = tmp_file.path;
    LOG("Arguments from {}", tmp_file.path);
  }
  BOOL ret = CreateProcess(full_path.c_str(),
                           const_cast<char*>(args.c_str()),
                           nullptr,
                           nullptr,
                           1,
                           dw_creation_flags,
                           nullptr,
                           nullptr,
                           &si,
                           &pi);
  if (fd_stdout != -1) {
    close(fd_stdout);
    close(fd_stderr);
  }
  if (ret == 0) {
    DWORD error = GetLastError();
    LOG("failed to execute {}: {} ({})",
        full_path,
        util::win32_error_message(error),
        error);
    return -1;
  }
  if (job) {
    BOOL assign_success = AssignProcessToJobObject(job, pi.hProcess);
    if (!assign_success) {
      TerminateProcess(pi.hProcess, 1);

      DWORD error = GetLastError();
      LOG("failed to assign process to job object {}: {} ({})",
          full_path,
          util::win32_error_message(error),
          error);
      return -1;
    }
    ResumeThread(pi.hThread);
  }
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(job);
  if (!doreturn) {
    exit(exitcode);
  }
  return exitcode;
}

#else

// Execute a compiler backend, capturing all output to the given paths the full
// path to the compiler to run is in argv[0].
int
execute(Context& ctx,
        const char* const* argv,
        util::Fd&& fd_out,
        util::Fd&& fd_err)
{
  LOG("Executing {}", util::format_argv_for_logging(argv));

  posix_spawn_file_actions_t fa;

  CHECK_LIB_CALL(posix_spawn_file_actions_init, &fa);
  CHECK_LIB_CALL(posix_spawn_file_actions_adddup2, &fa, *fd_out, STDOUT_FILENO);
  CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &fa, *fd_out);
  CHECK_LIB_CALL(posix_spawn_file_actions_adddup2, &fa, *fd_err, STDERR_FILENO);
  CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &fa, *fd_err);

  int result;
  {
    SignalHandlerBlocker signal_handler_blocker;
    pid_t pid;
    extern char** environ;
    result = posix_spawn(
      &pid, argv[0], &fa, nullptr, const_cast<char* const*>(argv), environ);
    if (result == 0) {
      ctx.compiler_pid = pid;
    }
  }

  posix_spawn_file_actions_destroy(&fa);
  fd_out.close();
  fd_err.close();

  if (result != 0) {
    return -1;
  }

  int status;
  while ((result = waitpid(ctx.compiler_pid, &status, 0)) != ctx.compiler_pid) {
    if (result == -1 && errno == EINTR) {
      continue;
    }
    throw core::Fatal(FMT("waitpid failed: {}", strerror(errno)));
  }

  {
    SignalHandlerBlocker signal_handler_blocker;
    ctx.compiler_pid = 0;
  }

  if (WEXITSTATUS(status) == 0 && WIFSIGNALED(status)) {
    return -1;
  }

  return WEXITSTATUS(status);
}

void
execute_noreturn(const char* const* argv,
                 const std::filesystem::path& /*temp_dir*/)
{
  execv(argv[0], const_cast<char* const*>(argv));
}
#endif

std::string
find_executable(const Context& ctx,
                const std::string& name,
                const std::string& exclude_path)
{
  if (fs::path(name).is_absolute()) {
    return name;
  }

  std::string path_list = ctx.config.path();
  if (path_list.empty()) {
    path_list = getenv("PATH");
  }
  if (path_list.empty()) {
    LOG_RAW("No PATH variable");
    return {};
  }

  return find_executable_in_path(name, path_list, exclude_path).string();
}
