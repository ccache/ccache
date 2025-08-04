#pragma once

#include "ccache/storage/remote/remotestorage.hpp"
#include "ccache/util/environment.hpp"
#include "ccache/util/logging.hpp"

#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

namespace storage::remote::backend {
namespace fs = std::filesystem;

inline void
start_daemon(const std::string type,
             const fs::path& socket_path,
             const std::string& url,
             const std::vector<RemoteStorage::Backend::Attribute> attributes,
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
  // TODO for later find_executable
  //            find_executable_in_path("executable", {"path1", "path2"});
  helper_exec = // "/etc/libexec/ccache-backend";
    "/home/rocky/repos/py_server_script/bin" / fs::path("ccache-backend-" + type);
#endif

  if (!fs::exists(helper_exec)) { // TODO
  }

  // extern char **environ;
  util::setenv("_CCACHE_REMOTE_URL", url);
  util::setenv("_CCACHE_SOCKET_PATH", socket_path.generic_string());
  util::setenv("_CCACHE_BUFFER_SIZE", std::to_string(buffer_size));
  util::setenv("_CCACHE_NUM_ATTR", std::to_string(attributes.size()));

  for (size_t i = 0; i < attributes.size(); i++) {
    std::string k = std::to_string(i);
    util::setenv("_CCACHE_ATTR_KEY_" + k, attributes[i].key);
    util::setenv("_CCACHE_ATTR_VALUE_" + k, attributes[i].value);
  }

#ifdef _WIN32
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  std::string commandLine = helper_exec.string() + " 2>&1";
  // + " " + socketFlag + " " + bufferFlag + " " + urlFlag + " 2>&1";
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
    int exres = execle(helper_exec.c_str(),
           helper_exec.c_str(),
           "2>&1",
           nullptr, // arguments until a NULL pointer
           environ);
    LOG("DEBUG Failure {} to start helper process {}",
        exres, helper_exec.generic_string());
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    LOG("DEBUG Failed to fork process {}", helper_exec.generic_string());
    exit(EXIT_FAILURE);
  }
#endif
}
} // namespace storage::remote::backend
