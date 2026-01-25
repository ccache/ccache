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

#pragma once

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <chrono>
#include <string>

namespace util {

// Error type for IPC channel operations.
struct IpcError
{
  enum class Failure {
    error,   // Permanent error (connection refused, invalid state, etc.)
    timeout, // Transient timeout (may succeed on retry)
  };

  Failure failure;
  std::string message;

  IpcError(Failure f, std::string msg)
    : failure(f),
      message(std::move(msg))
  {
  }
};

class IpcChannelClient
{
public:
  virtual ~IpcChannelClient() = default;

  virtual tl::expected<void, IpcError>
  connect(const std::string& endpoint,
          const std::chrono::milliseconds& timeout) = 0;

  virtual tl::expected<void, IpcError>
  send(nonstd::span<const uint8_t> data,
       const std::chrono::milliseconds& timeout) = 0;

  virtual tl::expected<size_t, IpcError>
  receive(nonstd::span<uint8_t> buffer,
          const std::chrono::milliseconds& timeout) = 0;

  virtual void close() = 0;
};

} // namespace util
