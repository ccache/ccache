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

// =============================================================================

// This is a storage helper used for ccache integration tests. It's
// intentionally simplistic and stupid: it fails early, keeps unbounded data in
// memory and only handles one client connection at a time.

// WARNING: You definitely don't want to base a real storage helper
// implementation on this code. Instead, have a look at other implementations
// listed on <https://ccache.dev/storage-helpers.html>.

#include <ccache/util/defer.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/wincompat.hpp>

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace fs = util::filesystem;

constexpr uint8_t PROTOCOL_VERSION = 0x01;
constexpr uint8_t CAP_GET_PUT_REMOVE_STOP = 0x00;

constexpr uint8_t STATUS_OK = 0x00;
constexpr uint8_t STATUS_NOOP = 0x01;
constexpr uint8_t STATUS_ERROR = 0x02;

constexpr uint8_t REQ_GET = 0x00;
constexpr uint8_t REQ_PUT = 0x01;
constexpr uint8_t REQ_REMOVE = 0x02;
constexpr uint8_t REQ_STOP = 0x03;

constexpr uint8_t PUT_FLAG_OVERWRITE = 0x01;

#ifdef _WIN32
using ConnHandle = HANDLE;
#else
using ConnHandle = int;
#endif

static FILE* g_log_file = nullptr;

static void
log_msg(const std::string& message)
{
  if (!g_log_file) {
    return;
  }

  const auto now = util::now();
  std::fprintf(g_log_file,
               "[%s.%06u] %s\n",
               util::format_iso8601_timestamp(now).c_str(),
               static_cast<unsigned int>(util::nsec_part(now) / 1000),
               message.c_str());
  std::fflush(g_log_file);
}

static void
fail(const std::string& message)
{
  log_msg(FMT("FATAL: {}", message));
  std::fprintf(stderr, "Error: %s\n", message.c_str());
  std::exit(1);
}

class IpcServer
{
public:
  IpcServer(const std::string& endpoint, std::chrono::seconds idle_timeout)
    : m_endpoint(endpoint),
      m_idle_timeout(idle_timeout),
      m_last_activity(util::now())
  {
  }

  void run();

private:
  std::string m_endpoint;
  std::chrono::seconds m_idle_timeout;
  util::TimePoint m_last_activity;
  std::unordered_map<std::string, std::vector<uint8_t>> m_storage;

  bool recv_exact(ConnHandle conn, uint8_t* buf, size_t count);
  void send_data(ConnHandle conn, const uint8_t* data, size_t len);
  void send_error(ConnHandle conn, const char* message);
  void handle_get(ConnHandle conn);
  void handle_put(ConnHandle conn);
  void handle_remove(ConnHandle conn);
  void handle_stop(ConnHandle conn);
  void handle_client(ConnHandle conn);

  bool m_running = false;
  bool check_idle_timeout();
  void update_activity();
};

void
IpcServer::update_activity()
{
  m_last_activity = util::now();
}

bool
IpcServer::check_idle_timeout()
{
  if (m_idle_timeout == std::chrono::seconds{0}) {
    return false;
  }

  auto idle_time = util::now() - m_last_activity;
  if (idle_time < m_idle_timeout) {
    return false;
  }

  log_msg("Idle timeout exceeded, shutting down");
  m_running = false;
  return true;
}

bool
IpcServer::recv_exact(ConnHandle conn, uint8_t* buf, size_t count)
{
  size_t received = 0;
  while (received < count) {
#ifdef _WIN32
    DWORD bytes_read = 0;
    if (!ReadFile(conn, buf + received, count - received, &bytes_read, nullptr)
        || bytes_read == 0) {
      return false;
    }
    received += bytes_read;
#else
    ssize_t n = recv(conn, buf + received, count - received, 0);
    if (n <= 0) {
      return false;
    }
    received += n;
#endif
  }
  return true;
}

void
IpcServer::send_data(ConnHandle conn, const uint8_t* data, size_t len)
{
#ifdef _WIN32
  DWORD bytes_written = 0;
  if (!WriteFile(conn, data, len, &bytes_written, nullptr)
      || bytes_written != len) {
    fail("Failed to send data to client");
  }
#else
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(conn, data + sent, len - sent, 0);
    if (n <= 0) {
      fail("Failed to send data to client");
    }
    sent += n;
  }
#endif
}

void
IpcServer::send_error(ConnHandle conn, const char* message)
{
  size_t len = std::strlen(message);
  if (len > 255) {
    len = 255;
  }

  std::vector<uint8_t> response;
  response.push_back(STATUS_ERROR);
  response.push_back(static_cast<uint8_t>(len));
  response.insert(response.end(), message, message + len);

  send_data(conn, response.data(), response.size());
}

void
IpcServer::handle_get(ConnHandle conn)
{
  uint8_t key_len;
  if (!recv_exact(conn, &key_len, 1)) {
    return;
  }

  std::string key;
  if (key_len > 0) {
    key.resize(key_len);
    if (!recv_exact(conn, reinterpret_cast<uint8_t*>(key.data()), key_len)) {
      return;
    }
  }

  log_msg(FMT("GET: key_len={}", key_len));

  auto it = m_storage.find(key);
  if (it != m_storage.end()) {
    const auto& value = it->second;
    std::vector<uint8_t> response;
    response.push_back(STATUS_OK);

    uint64_t value_len = value.size();
    uint8_t len_bytes[8];
    std::memcpy(len_bytes, &value_len, 8);
    response.insert(response.end(), len_bytes, len_bytes + 8);

    response.insert(response.end(), value.begin(), value.end());
    send_data(conn, response.data(), response.size());
    log_msg(FMT("  -> found, value_len={}", value.size()));
  } else {
    uint8_t response = STATUS_NOOP;
    send_data(conn, &response, 1);
    log_msg("  -> not found");
  }
}

void
IpcServer::handle_put(ConnHandle conn)
{
  uint8_t key_len;
  if (!recv_exact(conn, &key_len, 1)) {
    return;
  }

  std::string key;
  if (key_len > 0) {
    key.resize(key_len);
    if (!recv_exact(conn, reinterpret_cast<uint8_t*>(key.data()), key_len)) {
      return;
    }
  }

  uint8_t flags;
  if (!recv_exact(conn, &flags, 1)) {
    return;
  }
  bool overwrite = (flags & PUT_FLAG_OVERWRITE) != 0;

  uint8_t value_len_bytes[8];
  if (!recv_exact(conn, value_len_bytes, 8)) {
    return;
  }

  uint64_t value_len;
  std::memcpy(&value_len, value_len_bytes, 8);

  std::vector<uint8_t> value;
  if (value_len > 0) {
    value.resize(value_len);
    if (!recv_exact(conn, value.data(), value_len)) {
      return;
    }
  }

  log_msg(FMT("PUT: key_len={}, value_len={}, overwrite={}",
              key_len,
              value_len,
              overwrite));

  bool should_store = overwrite || m_storage.find(key) == m_storage.end();
  if (should_store) {
    m_storage[key] = std::move(value);
    uint8_t response = STATUS_OK;
    send_data(conn, &response, 1);
    log_msg("  -> stored");
  } else {
    uint8_t response = STATUS_NOOP;
    send_data(conn, &response, 1);
    log_msg("  -> not stored (key exists, no overwrite)");
  }
}

void
IpcServer::handle_remove(ConnHandle conn)
{
  uint8_t key_len;
  if (!recv_exact(conn, &key_len, 1)) {
    return;
  }

  std::string key;
  if (key_len > 0) {
    key.resize(key_len);
    if (!recv_exact(conn, reinterpret_cast<uint8_t*>(key.data()), key_len)) {
      return;
    }
  }

  log_msg(FMT("REMOVE: key_len={}", key_len));

  auto it = m_storage.find(key);
  if (it != m_storage.end()) {
    m_storage.erase(it);
    uint8_t response = STATUS_OK;
    send_data(conn, &response, 1);
    log_msg("  -> removed");
  } else {
    uint8_t response = STATUS_NOOP;
    send_data(conn, &response, 1);
    log_msg("  -> not removed (not found)");
  }
}

void
IpcServer::handle_stop(ConnHandle conn)
{
  log_msg("STOP: shutting down");
  uint8_t response = STATUS_OK;
  send_data(conn, &response, 1);
  m_running = false;
}

void
IpcServer::handle_client(ConnHandle conn)
{
  uint8_t greeting[3] = {PROTOCOL_VERSION, 1, CAP_GET_PUT_REMOVE_STOP};
  send_data(conn, greeting, sizeof(greeting));

  while (true) {
    uint8_t request_type;
    if (!recv_exact(conn, &request_type, 1)) {
      break;
    }

    update_activity();

    switch (request_type) {
    case REQ_GET:
      handle_get(conn);
      break;

    case REQ_PUT:
      handle_put(conn);
      break;

    case REQ_REMOVE:
      handle_remove(conn);
      break;

    case REQ_STOP:
      handle_stop(conn);
      return;

    default:
      log_msg(FMT("Unknown request type: {}", request_type));
      send_error(conn, "Unknown request type");
      return;
    }
  }
}

#ifdef _WIN32

void
IpcServer::run()
{
  HANDLE pipe =
    CreateNamedPipeA(m_endpoint.c_str(),
                     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                     1,    // Max instances
                     8192, // Output buffer
                     8192, // Input buffer
                     0,    // Default timeout
                     nullptr);

  if (pipe == INVALID_HANDLE_VALUE) {
    fail("CreateNamedPipe failed");
  }
  DEFER(CloseHandle(pipe));

  OVERLAPPED overlapped = {};
  overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) {
    fail("CreateEvent failed");
  }
  DEFER(CloseHandle(overlapped.hEvent));

  log_msg(FMT("IPC server listening on {}", m_endpoint));

  m_running = true;
  while (m_running) {
    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (!connected) {
      DWORD error = GetLastError();
      if (error == ERROR_IO_PENDING) {
        DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 100);
        if (wait_result == WAIT_TIMEOUT) {
          CancelIo(pipe);
          if (check_idle_timeout()) {
            break;
          }
          continue;
        } else if (wait_result != WAIT_OBJECT_0) {
          log_msg("WaitForSingleObject failed");
          break;
        }
        // Connection completed.
      } else if (error != ERROR_PIPE_CONNECTED) {
        log_msg(FMT("ConnectNamedPipe failed: {}", error));
        break;
      }
      // ERROR_PIPE_CONNECTED means client connected between Create and Connect.
    }

    log_msg("Client connected");
    update_activity();
    handle_client(pipe);
    DisconnectNamedPipe(pipe);
    log_msg("Client disconnected");

    if (check_idle_timeout()) {
      break;
    }
  }
}

#else

void
IpcServer::run()
{
  if (m_endpoint.length() >= sizeof(sockaddr_un::sun_path)) {
    fail("Socket path too long");
  }

  (void)fs::remove(m_endpoint);

  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    fail("socket() failed");
  }
  DEFER(close(server_fd));

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, m_endpoint.data(), m_endpoint.length());
  DEFER((void)fs::remove(m_endpoint));

  mode_t old_umask = umask(0077);
  int ret =
    bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  umask(old_umask);
  if (ret < 0) {
    fail("bind() failed");
  }

  if (listen(server_fd, 5) < 0) {
    fail("listen() failed");
  }

  log_msg(FMT("IPC server listening on {}", m_endpoint));

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100'000;
  setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  m_running = true;
  while (m_running) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (check_idle_timeout()) {
          break;
        }
        continue;
      }
      log_msg(FMT("accept() failed: {}", strerror(errno)));
      break;
    }

    log_msg("Client connected");
    update_activity();
    handle_client(client_fd);
    close(client_fd);
    log_msg("Client disconnected");

    if (check_idle_timeout()) {
      break;
    }
  }
}

#endif

int
main()
{
  const char* log_path = std::getenv("CRSH_LOGFILE");
  if (log_path) {
    g_log_file = std::fopen(log_path, "a");
    if (!g_log_file) {
      fail(FMT("Failed to open log file {}", log_path));
    }
  }
  DEFER([] {
    if (g_log_file) {
      std::fclose(g_log_file);
    }
  });

  const char* ipc_endpoint = std::getenv("CRSH_IPC_ENDPOINT");
  if (!ipc_endpoint) {
    fail("CRSH_IPC_ENDPOINT environment variable not set");
  }

  std::string endpoint;
#ifdef _WIN32
  endpoint = std::string("\\\\.\\pipe\\") + ipc_endpoint;
#else
  endpoint = ipc_endpoint;
#endif

  const char* url = std::getenv("CRSH_URL");
  if (!url) {
    fail("CRSH_URL not set");
  }

  const char* idle_timeout_str = std::getenv("CRSH_IDLE_TIMEOUT");
  uint64_t idle_timeout = 0;
  if (idle_timeout_str) {
    auto value = util::parse_unsigned(idle_timeout_str);
    if (!value) {
      fail(FMT("Invalid CRSH_IDLE_TIMEOUT: {}", value.error()));
    }
    idle_timeout = *value;
  }

  log_msg("Starting");
  log_msg(FMT("IPC endpoint: {}", endpoint));
  log_msg(FMT("URL: {}", url));
  log_msg(FMT("Idle timeout: {}", idle_timeout));

  IpcServer helper(endpoint, std::chrono::seconds{idle_timeout});
  helper.run();

  log_msg("Shutdown complete");
  return 0;
}
