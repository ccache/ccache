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

#include "unixsocketclient.hpp"

#include <ccache/util/format.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace util {

namespace {

std::chrono::milliseconds
remaining_timeout(const std::chrono::steady_clock::time_point start_time,
                  const std::chrono::milliseconds timeout)
{
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start_time);
  return elapsed >= timeout ? std::chrono::milliseconds(0) : timeout - elapsed;
}

tl::expected<void, IpcError>
poll_with_timeout(int fd,
                  short events,
                  std::chrono::milliseconds timeout,
                  const char* action)
{
  const auto start_time = std::chrono::steady_clock::now();

  while (true) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;

    auto remaining = remaining_timeout(start_time, timeout);
    int poll_result = poll(&pfd, 1, static_cast<int>(remaining.count()));
    if (poll_result > 0) {
      return {};
    }
    if (poll_result == 0) {
      return tl::unexpected(
        IpcError(IpcError::Failure::timeout, FMT("{} timeout", action)));
    }
    if (errno != EINTR) {
      return tl::unexpected(IpcError(IpcError::Failure::error,
                                     FMT("Poll failed: {}", strerror(errno))));
    }
  }
}

} // namespace

UnixSocketClient::~UnixSocketClient()
{
  do_close();
}

tl::expected<void, IpcError>
UnixSocketClient::connect(const std::string& endpoint,
                          const std::chrono::milliseconds& timeout)
{
  if (m_fd != -1) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Socket already connected"));
  }

  if (endpoint.length() >= sizeof(sockaddr_un::sun_path)) {
    return tl::unexpected(IpcError(IpcError::Failure::error,
                                   FMT("Socket path too long ({} >= {}): {}",
                                       endpoint.length(),
                                       sizeof(sockaddr_un::sun_path),
                                       endpoint)));
  }

  // Create socket.
  m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (m_fd == -1) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error,
               FMT("Failed to create socket: {}", strerror(errno))));
  }

  // Make socket non-blocking.
  int flags = fcntl(m_fd, F_GETFL, 0);
  if (flags == -1 || fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    int saved_errno = errno;
    close();
    return tl::unexpected(IpcError(
      IpcError::Failure::error,
      FMT("Failed to set socket non-blocking: {}", strerror(saved_errno))));
  }

  // Create socket address.
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, endpoint.data(), endpoint.length());

  // Attempt to connect.
  int result =
    ::connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

  if (result == -1) {
    if (errno != EINPROGRESS && errno != EAGAIN && errno != EINTR) {
      int saved_errno = errno;
      close();
      return tl::unexpected(IpcError(
        IpcError::Failure::error,
        FMT("Connection to {} failed: {}", endpoint, strerror(saved_errno))));
    }

    // Connection in progress, wait for completion with timeout.
    auto poll_result = poll_with_timeout(m_fd, POLLOUT, timeout, "Connection");
    if (!poll_result) {
      close();
      return tl::unexpected(poll_result.error());
    }

    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
      int saved_errno = errno;
      close();
      return tl::unexpected(
        IpcError(IpcError::Failure::error,
                 FMT("Failed to get socket error: {}", strerror(saved_errno))));
    }
    if (error != 0) {
      close();
      return tl::unexpected(
        IpcError(IpcError::Failure::error,
                 FMT("Connection failed: {}", strerror(error))));
    }
  }

  return {};
}

tl::expected<void, IpcError>
UnixSocketClient::send(std::span<const uint8_t> data,
                       const std::chrono::milliseconds& timeout)
{
  if (m_fd == -1) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Socket not connected"));
  }

  if (data.empty()) {
    return {};
  }

  size_t total_sent = 0;
  const auto start_time = std::chrono::steady_clock::now();

  while (total_sent < data.size()) {
    ssize_t sent =
      ::send(m_fd, data.data() + total_sent, data.size() - total_sent, 0);

    if (sent > 0) {
      total_sent += sent;
    } else if (sent == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno != EAGAIN) {
        return tl::unexpected(IpcError(
          IpcError::Failure::error, FMT("Send failed: {}", strerror(errno))));
      }

      // Calculate remaining timeout.
      const auto elapsed = std::chrono::steady_clock::now() - start_time;
      const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
      if (elapsed_ms >= timeout) {
        return tl::unexpected(
          IpcError(IpcError::Failure::timeout, "Send timeout"));
      }

      const auto remaining = timeout - elapsed_ms;

      // Wait for socket to become writable.
      auto poll_result = poll_with_timeout(m_fd, POLLOUT, remaining, "Send");
      if (!poll_result) {
        return tl::unexpected(poll_result.error());
      }
    }
  }

  return {};
}

tl::expected<size_t, IpcError>
UnixSocketClient::receive(std::span<uint8_t> buffer,
                          const std::chrono::milliseconds& timeout)
{
  if (m_fd == -1) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Socket not connected"));
  }

  if (buffer.empty()) {
    return 0;
  }

  auto poll_result = poll_with_timeout(m_fd, POLLIN, timeout, "Receive");
  if (!poll_result) {
    return tl::unexpected(poll_result.error());
  }

  // Receive data.
  while (true) {
    ssize_t received = ::recv(m_fd, buffer.data(), buffer.size(), 0);

    if (received == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        // This shouldn't happen after poll() indicates readiness, but handle
        // it.
        return tl::unexpected(
          IpcError(IpcError::Failure::error,
                   "Receive would block after poll indicated readiness"));
      }
      return tl::unexpected(IpcError(
        IpcError::Failure::error, FMT("Receive failed: {}", strerror(errno))));
    } else if (received == 0) {
      return tl::unexpected(
        IpcError(IpcError::Failure::error, "Connection closed by peer"));
    }

    return static_cast<size_t>(received);
  }
}

void
UnixSocketClient::close()
{
  do_close();
}

void
UnixSocketClient::do_close()
{
  if (m_fd != -1) {
    ::close(m_fd);
    m_fd = -1;
  }
}

} // namespace util
