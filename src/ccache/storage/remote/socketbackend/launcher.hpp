#pragma once

#include "ccache/util/logging.hpp"

#include <unistd.h>

#include <cstddef>
#include <string>

namespace storage::remote::backend {
namespace fs = std::filesystem;

inline void
start_daemon(const std::string type,
             const fs::path& socket_path,
             const std::string& url,
             const size_t buffer_size)
{
  fs::path helper_exec;

#ifdef _WIN32
  PWSTR path_tmp;
  if (SUCCEEDED(
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp))) {
    helper_exec = fs::path(path_tmp) / ("ccache-backend-" + type);
    CoTaskMemFree(path_tmp);
  } else {
    LOG("Failed to get known folder path! ERROR {}", "WIN32");
    return;
  }
#else
  helper_exec = // "/etc/libexec/ccache-backend";
    "/home/rocky/repos/py_server_script" / fs::path("ccache-backend-" + type);
#endif

  if (!fs::exists(helper_exec)) { // TODO
  }

  std::string socket_flag = "--socket=" + socket_path.generic_string();
  std::string buffer_flag = "--bufsize=" + std::to_string(buffer_size);
  std::string url_flag = "--url=" + url;

#ifdef _WIN32
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  std::string commandLine = helper_exec.string() + " " + socketFlag + " "
                            + bufferFlag + " " + urlFlag + " 2>&1";
  if (!CreateProcessA(nullptr,
                      const_cast<char*>(commandLine.c_str()),
                      nullptr,
                      nullptr,
                      FALSE,
                      0,
                      nullptr,
                      nullptr,
                      &si,
                      &pi)) {
    LOG("Failed to start helper process {}", helper_exec.generic_string());
    return;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
#else
  pid_t pid = fork();
  if (pid == 0) { // Child process
    execlp(helper_exec.c_str(),
           helper_exec.c_str(),
           socket_flag.c_str(),
           buffer_flag.c_str(),
           url_flag.c_str(),
           "2>&1",
           nullptr);
    LOG("DEBUG Failed to start helper process {}",
        helper_exec.generic_string());
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    LOG("DEBUG Failed to fork process {}", helper_exec.generic_string());
    exit(EXIT_FAILURE);
  }
#endif
}
} // namespace storage::remote::backend