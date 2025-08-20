#pragma once

#include "ccache/storage/remote/remotestorage.hpp"
#include "ccache/util/environment.hpp"
#include "ccache/util/logging.hpp"

#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace storage::remote::backend {
namespace fs = std::filesystem;

const std::vector<fs::path> k_fix_paths_to_search{
#ifdef _WIN32
  "C:\\Program Files\\ccache",
  "C:\\Program Files (x86)\\ccache",
#else
  "/usr/local/libexec/ccache", "/usr/libexec/ccache"
#endif
};

fs::path
search_for_executable(const std::string& name, const fs::path& dir)
{
  if (dir.empty()) {
    return {};
  }

  const std::vector<fs::path> candidates = {
    dir / name,
#ifdef _WIN32
    dir / FMT("{}.exe", name),
#endif
  };
  for (const auto& candidate : candidates) {
    const bool candidate_exists =
#ifdef _WIN32
      util::DirEntry(candidate).is_regular_file();
#else
      access(candidate.c_str(), X_OK) == 0;
#endif
    if (candidate_exists) {
      return candidate;
    }
  }

  return {};
}

// Looks for the executable binary for helper process
//
// (a) in the directory where the ccache binary is located
// (b) in $PATH, and
// (c) in ccache's libexec path.
fs::path
find_remote_helper(const std::string& executable_name)
{
  // find in ccache binary directory (current directory)
  if (auto result = search_for_executable(executable_name, fs::current_path());
      !result.empty()) {
    return result;
  }

  // find in PATH
  for (const auto& dir : util::getenv_path_list("PATH")) {
    if (auto result = search_for_executable(executable_name, dir);
        !result.empty()) {
      return result;
    }
  }

  // look into the libexec directory
  for (const auto& dir : k_fix_paths_to_search) {
    if (auto result = search_for_executable(executable_name, dir);
        !result.empty()) {
      return result;
    }
  }

  return {};
}

inline bool
start_daemon(const std::string type,
             const fs::path& socket_path,
             const std::string& url,
             const std::vector<RemoteStorage::Backend::Attribute>& attributes,
             const size_t buffer_size)
{
  const std::string executable_name = "ccache-" + type + "-storage";
  const fs::path helper_exec = find_remote_helper(executable_name);
  if (!fs::exists(helper_exec)) {
    LOG("No storage executable found for scheme '{}'!", type);
    return false; // return false TODO
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

  std::string command_line = helper_exec.string() + " 2>&1";
  if (!CreateProcessA(nullptr,
                      const_cast<char*>(command_line.c_str()),
                      nullptr,
                      nullptr,
                      FALSE,
                      0,
                      nullptr,
                      nullptr,
                      &si,
                      &pi)) {
    LOG("Failed to start helper process {}", helper_exec.generic_string());
    return false;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
#else
  pid_t pid = fork();
  if (pid == 0) { // Child process
    int exres = execle(helper_exec.c_str(),
                       helper_exec.c_str(),
                       util::logging::enabled() ? "--debug=1" : "",
                       "2>&1",
                       nullptr, // arguments until a NULL pointer
                       environ);
    LOG("DEBUG Failure {} to start helper process {}",
        exres,
        helper_exec.generic_string());
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    LOG("DEBUG Failed to fork process {}", helper_exec.generic_string());
    exit(EXIT_FAILURE);
  }
#endif
  return true;
}
} // namespace storage::remote::backend
