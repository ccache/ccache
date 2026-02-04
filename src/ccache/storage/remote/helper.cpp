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

#include "helper.hpp"

#include <ccache/config.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/remote/client.hpp>
#include <ccache/util/defer.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/error.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/lockfile.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/process.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/timer.hpp>
#include <ccache/util/wincompat.hpp>

#ifndef _WIN32
#  include <dirent.h>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <thread>

#ifndef environ
DLLIMPORT extern char** environ;
#endif

using namespace std::chrono_literals;

namespace fs = util::filesystem;

namespace storage::remote {

namespace {

#ifdef _WIN32
constexpr std::string_view k_named_pipe_prefix = "\\\\.\\pipe\\";
#endif

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

// Generate a user-specific, unique socket/pipe path based on URL and
// attributes.
std::string
generate_endpoint_name(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  static const uint8_t delimiter[1] = {0};

  Hash hash;
#ifdef _WIN32
  char username[256];
  DWORD username_len = sizeof(username);
  if (GetUserNameA(username, &username_len)) {
    hash.hash(username);
  }
#else
  hash.hash(static_cast<int64_t>(getuid()));
#endif
  hash.hash(delimiter);
  hash.hash(url.str());
  for (const auto& attr : attributes) {
    hash.hash(delimiter);
    hash.hash(attr.key);
    hash.hash(delimiter);
    hash.hash(attr.value);
  }
  return FMT("storage-{}-{}", url.scheme(), util::format_base16(hash.digest()));
}

#ifndef _WIN32
// Choose a short and safe base directory for Unix socket.
//
// Rationale:
// - Unix socket paths have a strict length limit (sun_path).
// - The ccache configured temporary dir can become very long (e.g. in CI).
// - We want a directory that is private to the user to avoid other users
//   squatting the socket name.
std::optional<fs::path>
get_helper_ipc_dir()
{
  // If XDG_RUNTIME_DIR is set, use the same location as
  // Config::default_temporary_dir.
  if (const auto dir = Config::get_xdg_runtime_tmp_dir(); !dir.empty()) {
    return dir;
  }

  // Otherwise, create a per-user private directory under /tmp. We intentionally
  // use /tmp instead of $TMPDIR to keep socket paths short.
  fs::path dir = FMT("/tmp/ccache-tmp-{}", static_cast<uint64_t>(getuid()));
  if (auto r = fs::create_directories(dir); !r) {
    LOG("Failed to create helper IPC dir {}: {}", dir, r.error());
    return std::nullopt;
  }

  // Ensure correct permissions regardless of umask.
  if (chmod(dir.c_str(), 0700) != 0) {
    LOG("Failed to chmod helper IPC dir {}: {}", dir, strerror(errno));
    return std::nullopt;
  }

  util::DirEntry entry(dir);
  if (!entry) {
    LOG("Failed to stat helper IPC dir {}: {}",
        dir,
        strerror(entry.error_number()));
    return std::nullopt;
  }
  if (!entry.is_directory() || entry.is_symlink()) {
    LOG("Helper IPC dir {} is not a directory", dir);
    return std::nullopt;
  }
  if ((entry.mode() & 0077) != 0) {
    LOG("Helper IPC dir {} is not private (mode {:o})", dir, entry.mode());
    return std::nullopt;
  }

  return dir;
}
#endif

std::vector<std::string>
build_helper_env(
  const Url& url,
  std::string_view ipc_endpoint,
  std::chrono::milliseconds idle_timeout,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  std::vector<std::string> env_vars;
  env_vars.emplace_back(FMT("CRSH_IPC_ENDPOINT={}", ipc_endpoint));
  env_vars.emplace_back(FMT("CRSH_URL={}", url.str()));
  env_vars.emplace_back(
    FMT("CRSH_IDLE_TIMEOUT={}", idle_timeout.count() / 1000));
  env_vars.emplace_back(FMT("CRSH_NUM_ATTR={}", attributes.size()));

  for (size_t i = 0; i < attributes.size(); ++i) {
    env_vars.emplace_back(FMT("CRSH_ATTR_KEY_{}={}", i, attributes[i].key));
    env_vars.emplace_back(FMT("CRSH_ATTR_VALUE_{}={}", i, attributes[i].value));
  }

  return env_vars;
}

bool
is_ccache_crsh_var(std::string_view entry)
{
  auto [name, value] = util::split_once(entry, '=');
  if (!value) {
    return false;
  }

  return name == "CRSH_IPC_ENDPOINT" || name == "CRSH_URL"
         || name == "CRSH_IDLE_TIMEOUT" || name == "CRSH_NUM_ATTR"
         || name.starts_with("CRSH_ATTR_KEY_")
         || name.starts_with("CRSH_ATTR_VALUE_");
}

#ifndef _WIN32
std::vector<int>
get_fds_to_close()
{
  std::vector<int> fds_to_close;

#  ifdef __linux__
  // Enumerate open FDs via /proc/self/fd for efficiency. (Using opendir instead
  // of std::filesystem to be able to filter out dir_fd.)
  bool enumerated = false;
  if (DIR* dir = opendir("/proc/self/fd"); dir) {
    enumerated = true;
    const int dir_fd = dirfd(dir);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_name[0] == '.') {
        continue; // skip . and ..
      }
      char* endptr;
      long fd = std::strtol(entry->d_name, &endptr, 10);
      if (*endptr == '\0' && fd >= 3 && fd != dir_fd) {
        fds_to_close.push_back(static_cast<int>(fd));
      }
    }
    closedir(dir);
  }

  if (enumerated) {
    return fds_to_close;
  }
#  endif

  // Fallback: check FDs up to a reasonable limit.
  long max_fd = sysconf(_SC_OPEN_MAX);
  if (max_fd < 0 || max_fd > 1024) {
    max_fd = 1024; // cap to avoid thousands of fcntl syscalls
  }
  for (int fd = 3; fd < max_fd; ++fd) {
    // We must verify that the FD exists because on some systems (e.g. macOS),
    // posix_spawn will fail when trying to close a non-existent FD.
    if (fcntl(fd, F_GETFD) != -1) {
      fds_to_close.push_back(fd);
    }
  }

  return fds_to_close;
}
#endif

tl::expected<void, std::string>
spawn_helper(const fs::path& helper_path,
             std::string_view endpoint,
             const Url& url,
             std::chrono::milliseconds idle_timeout,
             const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  LOG("Spawning storage helper {} for {}", helper_path, endpoint);

#ifdef _WIN32
  // Don't pass \\.\pipe\ prefix on Windows.
  DEBUG_ASSERT(endpoint.starts_with(k_named_pipe_prefix));
  auto ipc_endpoint = endpoint.substr(k_named_pipe_prefix.length());
#else
  const auto& ipc_endpoint = endpoint;
#endif

  const auto env_vars =
    build_helper_env(url, ipc_endpoint, idle_timeout, attributes);

#ifdef _WIN32
  std::string env_block;
  for (char** env = environ; *env != nullptr; ++env) {
    if (!is_ccache_crsh_var(*env)) {
      env_block += *env;
      env_block += '\0';
    }
  }
  for (const auto& var : env_vars) {
    env_block += var;
    env_block += '\0';
  }
  env_block += '\0';

  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  std::string application = helper_path.string();
  // CreateProcess may write into lpCommandLine, so it should be a mutable
  // buffer != application.
  std::string cmdline = application;
  if (!CreateProcess(application.c_str(),
                     cmdline.data(),
                     nullptr,
                     nullptr,
                     FALSE,
                     CREATE_NO_WINDOW | DETACHED_PROCESS,
                     env_block.data(),
                     nullptr,
                     &si,
                     &pi)) {
    DWORD error = GetLastError();
    return tl::unexpected(
      FMT("{} ({})", util::win32_error_message(error), error));
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
#else
  std::vector<std::string> env_strings;
  for (char** env = environ; *env != nullptr; ++env) {
    if (!is_ccache_crsh_var(*env)) {
      env_strings.emplace_back(*env);
    }
  }
  env_strings.insert(env_strings.end(), env_vars.begin(), env_vars.end());

  std::vector<char*> env_ptrs;
  env_ptrs.reserve(env_strings.size() + 1);
  for (auto& str : env_strings) {
    env_ptrs.push_back(const_cast<char*>(str.c_str()));
  }
  env_ptrs.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  CHECK_LIB_CALL(posix_spawn_file_actions_init, &actions);
  DEFER(posix_spawn_file_actions_destroy(&actions));

  CHECK_LIB_CALL(
    posix_spawn_file_actions_addopen, &actions, 0, "/dev/null", O_RDONLY, 0);
  CHECK_LIB_CALL(
    posix_spawn_file_actions_addopen, &actions, 1, "/dev/null", O_WRONLY, 0);
  CHECK_LIB_CALL(
    posix_spawn_file_actions_addopen, &actions, 2, "/dev/null", O_WRONLY, 0);

  // We need to close all inherited FDs since keeping them open in the
  // long-lived helper process can interfere with build systems, see for example
  // <https://github.com/ninja-build/ninja/issues/2052>.
  for (int fd : get_fds_to_close()) {
    CHECK_LIB_CALL(posix_spawn_file_actions_addclose, &actions, fd);
  }

  posix_spawnattr_t attr;
  CHECK_LIB_CALL(posix_spawnattr_init, &attr);
  DEFER(posix_spawnattr_destroy(&attr));

  // Create a new session to fully detach from the controlling terminal.
#  ifdef POSIX_SPAWN_SETSID
  CHECK_LIB_CALL(posix_spawnattr_setflags, &attr, POSIX_SPAWN_SETSID);
#  else
  // Fallback for systems without POSIX_SPAWN_SETSID.
  CHECK_LIB_CALL(posix_spawnattr_setflags, &attr, POSIX_SPAWN_SETPGROUP);
  CHECK_LIB_CALL(posix_spawnattr_setpgroup, &attr, 0);
#  endif

  pid_t pid;
  char* argv[] = {const_cast<char*>(helper_path.c_str()), nullptr};
  int result = posix_spawnp(
    &pid, helper_path.c_str(), &actions, &attr, argv, env_ptrs.data());

  if (result != 0) {
    return tl::unexpected(strerror(result));
  }

  LOG("Spawned helper process with PID {}", pid);
#endif

  return {};
}

// Backend implementation that communicates with a helper process.
class HelperBackend : public RemoteStorage::Backend
{
public:
  HelperBackend(const fs::path& helper_path,
                const fs::path& temp_dir,
                const Url& url,
                const std::vector<Backend::Attribute>& attributes,
                std::chrono::milliseconds data_timeout,
                std::chrono::milliseconds request_timeout,
                std::chrono::milliseconds idle_timeout);

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  std::span<const uint8_t> value,
                                  Overwrite overwrite) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

  void stop() override;

private:
  fs::path m_helper_path;
  std::string m_endpoint;        // Unix socket on POSIX, pipe name on Windows
  fs::path m_endpoint_lock_path; // path to lock for guarding spawn of helper
  Url m_url;
  std::vector<Backend::Attribute> m_attributes;
  std::chrono::milliseconds m_idle_timeout;
  Client m_client;
  bool m_connected = false;

  tl::expected<void, Failure> ensure_connected(bool spawn = true);
  tl::expected<void, Failure> finalize_connection();
};

HelperBackend::HelperBackend(const fs::path& helper_path,
                             const fs::path& temp_dir,
                             const Url& url,
                             const std::vector<Backend::Attribute>& attributes,
                             std::chrono::milliseconds data_timeout,
                             std::chrono::milliseconds request_timeout,
                             std::chrono::milliseconds idle_timeout)
  : m_helper_path(helper_path),
    m_url(url),
    m_attributes(attributes),
    m_idle_timeout(idle_timeout),
    m_client(data_timeout, request_timeout)
{
  if (m_helper_path.empty()) {
    // The "crsh:" URL case:
#ifdef _WIN32
    m_endpoint = FMT("{}{}", k_named_pipe_prefix, url.path());
#else
    m_endpoint = url.path();
#endif
    // No m_endpoint_lock_path needed since we won't spawn a helper.
  } else {
    // The common case:
    auto endpoint_name = generate_endpoint_name(url, attributes);
#ifdef _WIN32
    m_endpoint = FMT("{}ccache-{}", k_named_pipe_prefix, endpoint_name);
    m_endpoint_lock_path = FMT("{}/{}", temp_dir, endpoint_name);
#else
    auto helper_ipc_dir = get_helper_ipc_dir();
    if (!helper_ipc_dir) {
      LOG("Failed to select helper IPC dir, falling back to {}", temp_dir);
      helper_ipc_dir = temp_dir;
    }
    m_endpoint = FMT("{}/{}", *helper_ipc_dir, endpoint_name);
    m_endpoint_lock_path = m_endpoint;
#endif
  }
}

tl::expected<void, RemoteStorage::Backend::Failure>
HelperBackend::finalize_connection()
{
  if (m_client.protocol_version() != Client::k_protocol_version) {
    LOG("Unexpected remote storage helper protocol version: {} (!= {})",
        m_client.protocol_version(),
        Client::k_protocol_version);
    return tl::unexpected(Failure::error);
  }

  if (!m_client.has_capability(Client::Capability::get_put_remove_stop)) {
    LOG_RAW("Remote storage helper does not support capability 0");
    return tl::unexpected(Failure::error);
  }

  m_connected = true;
  return {};
}

tl::expected<void, RemoteStorage::Backend::Failure>
HelperBackend::ensure_connected(bool spawn)
{
  if (m_connected) {
    return {};
  }

  // Try to connect to an existing helper.
  util::Timer timer;
  auto connect_result = m_client.connect(m_endpoint);
  if (connect_result) {
    LOG("Connected to existing remote storage helper at {} ({:.2f} ms)",
        m_endpoint,
        timer.measure_ms());
    return finalize_connection();
  }
  LOG(
    "Failed to connect to existing remote storage helper at {}: {} ({:.2f} ms)",
    m_endpoint,
    connect_result.error().message,
    timer.measure_ms());

  if (!spawn) {
    return {};
  }

  if (m_helper_path.empty()) {
    // Could not connect to "crsh:" endpoint, so just fail.
    return tl::unexpected(Failure::error);
  }

  // No existing helper, spawn a new one. Use a lock file to prevent multiple
  // processes from spawning simultaneously.
  util::LockFile spawn_lock(m_endpoint_lock_path);
  if (!spawn_lock.acquire()) {
    LOG_RAW("Failed to acquire spawn lock");
    return tl::unexpected(Failure::error);
  }

  // We have the lock. Check again if another process spawned while we waited.
  timer.reset();
  if (m_client.connect(m_endpoint)) {
    LOG(
      "Connected to remote storage helper spawned by another process ({:.2f}"
      " ms)",
      timer.measure_ms());
    return finalize_connection();
  }

  // No helper exists, spawn it now.
  timer.reset();
  auto spawn_result = spawn_helper(
    m_helper_path, m_endpoint, m_url, m_idle_timeout, m_attributes);
  if (!spawn_result) {
    LOG("Failed to spawn helper: {}", spawn_result.error());
    return tl::unexpected(Failure::error);
  }
  LOG("Spawned remote storage helper ({:.2f} ms)", timer.measure_ms());
  timer.reset();

  constexpr auto sleep_duration = 1ms;
  constexpr double spawn_timeout_ms = 1000.0;

  timer.reset();
  while (timer.measure_ms() < spawn_timeout_ms) {
    connect_result = m_client.connect(m_endpoint);
    if (connect_result) {
      LOG("Connected to newly spawned remote storage helper at {} ({:.2f} ms)",
          m_endpoint,
          timer.measure_ms());
      return finalize_connection();
    }

    std::this_thread::sleep_for(sleep_duration);
  }

  LOG("Failed to connect to spawned remote storage helper: {}",
      connect_result.error().message);

  return tl::unexpected(Failure::timeout);
}

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
HelperBackend::get(const Hash::Digest& key)
{
  TRY(ensure_connected());

  auto result = m_client.get(key);
  if (!result) {
    const auto& error = result.error();
    LOG("Remote storage get failed: {}", error.message);
    auto failure = (error.failure == Client::Failure::timeout)
                     ? Failure::timeout
                     : Failure::error;
    return tl::unexpected(failure);
  }

  return *result;
}

tl::expected<bool, RemoteStorage::Backend::Failure>
HelperBackend::put(const Hash::Digest& key,
                   std::span<const uint8_t> value,
                   Overwrite overwrite)
{
  TRY(ensure_connected());

  Client::PutFlags flags;
  flags.overwrite = (overwrite == Overwrite::yes);

  auto result = m_client.put(key, value, flags);
  if (!result) {
    const auto& error = result.error();
    LOG("Remote storage put failed: {}", error.message);
    auto failure = (error.failure == Client::Failure::timeout)
                     ? Failure::timeout
                     : Failure::error;
    return tl::unexpected(failure);
  }

  return *result;
}

tl::expected<bool, RemoteStorage::Backend::Failure>
HelperBackend::remove(const Hash::Digest& key)
{
  TRY(ensure_connected());

  auto result = m_client.remove(key);
  if (!result) {
    const auto& error = result.error();
    LOG("Remote storage remove failed: {}", error.message);
    auto failure = (error.failure == Client::Failure::timeout)
                     ? Failure::timeout
                     : Failure::error;
    return tl::unexpected(failure);
  }

  return *result;
}

void
HelperBackend::stop()
{
  if (auto r = ensure_connected(false); !r) {
    LOG_RAW("Failed to connect to remote storage helper");
    return;
  }
  if (!m_connected) {
    LOG("No need to stop remote storage helper for {}", m_url.str());
    return;
  }
  if (auto r = m_client.stop(); r) {
    LOG("Stopped remote storage helper for {}", m_url.str());
  } else {
    LOG("Failed to stop remote storage helper for {}: {}",
        m_url.str(),
        r.error().message);
  }
}

} // namespace

Helper::Helper(const std::filesystem::path& helper_path,
               const fs::path& temp_dir,
               std::chrono::milliseconds data_timeout,
               std::chrono::milliseconds request_timeout,
               std::chrono::milliseconds idle_timeout)
  : m_helper_path(helper_path),
    m_temp_dir(temp_dir),
    m_data_timeout(data_timeout),
    m_request_timeout(request_timeout),
    m_idle_timeout(idle_timeout)
{
}

Helper::Helper(std::chrono::milliseconds data_timeout,
               std::chrono::milliseconds request_timeout)
  : m_data_timeout(data_timeout),
    m_request_timeout(request_timeout)
{
}

std::unique_ptr<RemoteStorage::Backend>
Helper::create_backend(const Url& url,
                       const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<HelperBackend>(m_helper_path,
                                         m_temp_dir,
                                         url,
                                         attributes,
                                         m_data_timeout,
                                         m_request_timeout,
                                         m_idle_timeout);
}

} // namespace storage::remote
