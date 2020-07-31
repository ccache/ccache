// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2011-2020 Joel Rosdahl and other contributors
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

#include "Config.hpp"
#include "Context.hpp"
#include "Fd.hpp"
#include "SignalHandler.hpp"
#include "Stat.hpp"
#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "logging.hpp"

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

using nonstd::string_view;

#ifdef _WIN32
int
execute(const char* const* argv, Fd&& fd_out, Fd&& fd_err, pid_t* /*pid*/)
{
  return win32execute(argv[0], argv, 1, fd_out.release(), fd_err.release());
}

std::string
win32getshell(const std::string& path)
{
  const char* path_env = getenv("PATH");
  std::string sh;
  if (Util::to_lowercase(Util::get_extension(path)) == ".sh" && path_env) {
    sh = find_executable_in_path("sh.exe", "", path_env);
  }
  if (sh.empty() && getenv("CCACHE_DETECT_SHEBANG")) {
    // Detect shebang.
    File fp(path, "r");
    if (fp) {
      char buf[10] = {0};
      fgets(buf, sizeof(buf) - 1, fp.get());
      if (std::string(buf) == "#!/bin/sh" && path_env) {
        sh = find_executable_in_path("sh.exe", "", path_env);
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
             int fd_stderr)
{
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

  std::string args = Win32Util::argv_to_string(argv, sh);
  std::string full_path = Win32Util::add_exe_suffix(path);
  std::string tmp_file_path;
  if (args.length() > 8192) {
    TemporaryFile tmp_file(path);
    Util::write_fd(*tmp_file.fd, args.data(), args.length());
    args = fmt::format("\"@{}\"", tmp_file.path);
    tmp_file_path = tmp_file.path;
  }
  BOOL ret = CreateProcess(full_path.c_str(),
                           const_cast<char*>(args.c_str()),
                           nullptr,
                           nullptr,
                           1,
                           0,
                           nullptr,
                           nullptr,
                           &si,
                           &pi);
  if (!tmp_file_path.empty()) {
    Util::unlink_tmp(tmp_file_path);
  }
  if (fd_stdout != -1) {
    close(fd_stdout);
    close(fd_stderr);
  }
  if (ret == 0) {
    DWORD error = GetLastError();
    cc_log("failed to execute %s: %s (%lu)",
           full_path.c_str(),
           Win32Util::error_message(error).c_str(),
           error);
    return -1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (!doreturn) {
    exit(exitcode);
  }
  return exitcode;
}

#else

// Execute a compiler backend, capturing all output to the given paths the full
// path to the compiler to run is in argv[0].
int
execute(const char* const* argv, Fd&& fd_out, Fd&& fd_err, pid_t* pid)
{
  cc_log_argv("Executing ", argv);

  {
    SignalHandlerBlocker signal_handler_blocker;
    *pid = fork();
  }

  if (*pid == -1) {
    fatal("Failed to fork: {}", strerror(errno));
  }

  if (*pid == 0) {
    // Child.
    dup2(*fd_out, STDOUT_FILENO);
    fd_out.close();
    dup2(*fd_err, STDERR_FILENO);
    fd_err.close();
    exit(execv(argv[0], const_cast<char* const*>(argv)));
  }

  fd_out.close();
  fd_err.close();

  int status;
  if (waitpid(*pid, &status, 0) != *pid) {
    fatal("waitpid failed: {}", strerror(errno));
  }

  {
    SignalHandlerBlocker signal_handler_blocker;
    *pid = 0;
  }

  if (WEXITSTATUS(status) == 0 && WIFSIGNALED(status)) {
    return -1;
  }

  return WEXITSTATUS(status);
}
#endif

std::string
find_executable(const Context& ctx,
                const std::string& name,
                const std::string& exclude_name)
{
  if (Util::is_absolute_path(name)) {
    return name;
  }

  std::string path = ctx.config.path();
  if (path.empty()) {
    path = getenv("PATH");
  }
  if (path.empty()) {
    cc_log("No PATH variable");
    return {};
  }

  return find_executable_in_path(name, exclude_name, path);
}

std::string
find_executable_in_path(const std::string& name,
                        const std::string& exclude_name,
                        const std::string& path)
{
  if (path.empty()) {
    return {};
  }

  // Search the path looking for the first compiler of the right name that isn't
  // us.
  for (const std::string& dir : Util::split_into_strings(path, PATH_DELIM)) {
#ifdef _WIN32
    char namebuf[MAX_PATH];
    int ret = SearchPath(
      dir.c_str(), name.c_str(), nullptr, sizeof(namebuf), namebuf, nullptr);
    if (!ret) {
      std::string exename = fmt::format("{}.exe", name);
      ret = SearchPath(dir.c_str(),
                       exename.c_str(),
                       nullptr,
                       sizeof(namebuf),
                       namebuf,
                       nullptr);
    }
    (void)exclude_name;
    if (ret) {
      return namebuf;
    }
#else
    assert(!exclude_name.empty());
    std::string fname = fmt::format("{}/{}", dir, name);
    auto st1 = Stat::lstat(fname);
    auto st2 = Stat::stat(fname);
    // Look for a normal executable file.
    if (st1 && st2 && st2.is_regular() && access(fname.c_str(), X_OK) == 0) {
      if (st1.is_symlink()) {
        std::string real_path = Util::real_path(fname, true);
        if (Util::base_name(real_path) == exclude_name) {
          // It's a link to "ccache"!
          continue;
        }
      }

      // Found it!
      return fname;
    }
#endif
  }

  return {};
}
