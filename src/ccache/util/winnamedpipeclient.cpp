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

#include "winnamedpipeclient.hpp"

#include <ccache/util/error.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/wincompat.hpp>

#include <chrono>

namespace util {

WinNamedPipeClient::WinNamedPipeClient()
  : m_handle(static_cast<void*>(INVALID_HANDLE_VALUE))
{
}

WinNamedPipeClient::~WinNamedPipeClient()
{
  do_close();
}

tl::expected<void, IpcError>
WinNamedPipeClient::connect(const std::string& endpoint,
                            const std::chrono::milliseconds& timeout)
{
  if (static_cast<HANDLE>(m_handle) != INVALID_HANDLE_VALUE) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Pipe already connected"));
  }

  // Try to connect with retries (server might not be ready yet).
  const auto start_time = std::chrono::steady_clock::now();
  while (true) {
    m_handle = static_cast<void*>(
      CreateFileA(endpoint.c_str(),
                  GENERIC_READ | GENERIC_WRITE,
                  0,       // No sharing
                  nullptr, // Default security
                  OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, // Use overlapped I/O for timeouts
                  nullptr));

    if (static_cast<HANDLE>(m_handle) != INVALID_HANDLE_VALUE) {
      // Successfully connected.
      break;
    }

    DWORD error = GetLastError();
    if (error != ERROR_PIPE_BUSY) {
      return tl::unexpected(IpcError(IpcError::Failure::error,
                                     FMT("Failed to connect to pipe {}: {}",
                                         endpoint,
                                         util::win32_error_message(error))));
    }

    // Check if timeout exceeded.
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    if (elapsed_ms >= timeout) {
      return tl::unexpected(
        IpcError(IpcError::Failure::timeout, "Connection timeout"));
    }

    // Wait for pipe to become available.
    const auto remaining = timeout - elapsed_ms;
    DWORD wait_time = static_cast<DWORD>(remaining.count());

    if (!WaitNamedPipeA(endpoint.c_str(), wait_time)) {
      DWORD wait_error = GetLastError();
      if (wait_error == ERROR_SEM_TIMEOUT) {
        return tl::unexpected(
          IpcError(IpcError::Failure::timeout, "Connection timeout"));
      }
      return tl::unexpected(IpcError(IpcError::Failure::error,
                                     FMT("Failed to wait for pipe: {}",
                                         win32_error_message(GetLastError()))));
    }
  }

  return {};
}

tl::expected<void, IpcError>
WinNamedPipeClient::send(nonstd::span<const uint8_t> data,
                         const std::chrono::milliseconds& timeout)
{
  if (m_handle == INVALID_HANDLE_VALUE) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Pipe not connected"));
  }

  if (data.empty()) {
    return {};
  }

  const auto start_time = std::chrono::steady_clock::now();

  size_t total_sent = 0;

  while (total_sent < data.size()) {
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
      return tl::unexpected(
        IpcError(IpcError::Failure::error,
                 FMT("Failed to create event: {}",
                     util::win32_error_message(GetLastError()))));
    }

    // Limit write size to avoid DWORD overflow.
    const size_t bytes_remaining = data.size() - total_sent;
    const DWORD max_write = std::numeric_limits<DWORD>::max();
    DWORD bytes_to_write = static_cast<DWORD>(
      std::min(bytes_remaining, static_cast<size_t>(max_write)));
    DWORD bytes_written;

    BOOL result = WriteFile(m_handle,
                            data.data() + total_sent,
                            bytes_to_write,
                            &bytes_written,
                            &overlapped);

    if (!result) {
      DWORD error = GetLastError();
      if (error != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        return tl::unexpected(IpcError(
          IpcError::Failure::error,
          FMT("WriteFile failed: {}", util::win32_error_message(error))));
      }

      const auto elapsed = std::chrono::steady_clock::now() - start_time;
      const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
      if (elapsed_ms >= timeout) {
        CancelIo(m_handle);
        CloseHandle(overlapped.hEvent);
        return tl::unexpected(
          IpcError(IpcError::Failure::timeout, "Send timeout"));
      }

      const auto remaining = timeout - elapsed_ms;
      DWORD wait_time = static_cast<DWORD>(remaining.count());

      DWORD wait_result = WaitForSingleObject(overlapped.hEvent, wait_time);
      if (wait_result == WAIT_TIMEOUT) {
        CancelIo(m_handle);
        CloseHandle(overlapped.hEvent);
        return tl::unexpected(
          IpcError(IpcError::Failure::timeout, "Send timeout"));
      } else if (wait_result != WAIT_OBJECT_0) {
        CloseHandle(overlapped.hEvent);
        return tl::unexpected(IpcError(
          IpcError::Failure::error,
          FMT("Wait failed: {}", util::win32_error_message(GetLastError()))));
      }

      if (!GetOverlappedResult(m_handle, &overlapped, &bytes_written, FALSE)) {
        CancelIo(m_handle);
        CloseHandle(overlapped.hEvent);
        return tl::unexpected(
          IpcError(IpcError::Failure::error,
                   FMT("GetOverlappedResult failed: {}",
                       util::win32_error_message(GetLastError()))));
      }
    }

    CloseHandle(overlapped.hEvent);
    total_sent += bytes_written;
  }

  return {};
}

tl::expected<size_t, IpcError>
WinNamedPipeClient::receive(nonstd::span<uint8_t> buffer,
                            const std::chrono::milliseconds& timeout)
{
  if (m_handle == INVALID_HANDLE_VALUE) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Pipe not connected"));
  }

  if (buffer.empty()) {
    return 0;
  }

  const auto timeout_ms = static_cast<DWORD>(timeout.count());

  OVERLAPPED overlapped = {};
  overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error,
               FMT("Failed to create event: {}",
                   util::win32_error_message(GetLastError()))));
  }

  // Limit read size to avoid DWORD overflow.
  const size_t buffer_size = buffer.size();
  const DWORD max_read = std::numeric_limits<DWORD>::max();
  DWORD bytes_to_read =
    static_cast<DWORD>(std::min(buffer_size, static_cast<size_t>(max_read)));

  DWORD bytes_read;
  BOOL result =
    ReadFile(m_handle, buffer.data(), bytes_to_read, &bytes_read, &overlapped);

  if (!result) {
    DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      CloseHandle(overlapped.hEvent);
      if (error == ERROR_BROKEN_PIPE) {
        return tl::unexpected(
          IpcError(IpcError::Failure::error, "Connection closed by peer"));
      }
      return tl::unexpected(
        IpcError(IpcError::Failure::error,
                 FMT("ReadFile failed: {}", util::win32_error_message(error))));
    }

    DWORD wait_result = WaitForSingleObject(overlapped.hEvent, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
      CancelIo(m_handle);
      CloseHandle(overlapped.hEvent);
      return tl::unexpected(
        IpcError(IpcError::Failure::timeout, "Receive timeout"));
    } else if (wait_result != WAIT_OBJECT_0) {
      CloseHandle(overlapped.hEvent);
      return tl::unexpected(IpcError(
        IpcError::Failure::error,
        FMT("Wait failed: {}", util::win32_error_message(GetLastError()))));
    }

    if (!GetOverlappedResult(m_handle, &overlapped, &bytes_read, FALSE)) {
      CloseHandle(overlapped.hEvent);
      DWORD overlapped_error = GetLastError();
      if (overlapped_error == ERROR_BROKEN_PIPE) {
        return tl::unexpected(
          IpcError(IpcError::Failure::error, "Connection closed by peer"));
      }
      return tl::unexpected(
        IpcError(IpcError::Failure::error,
                 FMT("GetOverlappedResult failed: {}",
                     util::win32_error_message(GetLastError()))));
    }
  }

  CloseHandle(overlapped.hEvent);

  if (bytes_read == 0) {
    return tl::unexpected(
      IpcError(IpcError::Failure::error, "Connection closed by peer"));
  }

  return static_cast<size_t>(bytes_read);
}

void
WinNamedPipeClient::close()
{
  do_close();
}

void
WinNamedPipeClient::do_close()
{
  if (m_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
  }
}

} // namespace util
