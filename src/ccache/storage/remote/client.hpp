// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License or (at your option)
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

#include <ccache/hash.hpp>
#include <ccache/util/bufferedipcchannelclient.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/noncopyable.hpp>

#ifdef _WIN32
#  include <ccache/util/winnamedpipeclient.hpp>
#else
#  include <ccache/util/unixsocketclient.hpp>
#endif

#include <tl/expected.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storage::remote {

// This class provides the ccache client side of the protocol described in
// doc/remote_storage_helper_spec.md.
class Client : util::NonCopyable
{
public:
  static constexpr uint8_t k_greeting_format_1 = 0x01;
  static constexpr uint8_t k_greeting_format_2 = 0x02;
  static constexpr uint8_t k_max_greeting_format = k_greeting_format_2;

  enum class Capability : uint8_t {
    get_put_remove_stop = 0x00, // get/put/remove/stop operations
  };

  enum class Status : uint8_t {
    ok = 0x00,   // Operation completed successfully
    noop = 0x01, // Operation not completed (key not found, not stored, etc.)
    error = 0x02 // Error occurred (bad parameters, network/server errors)
  };

  enum class Failure {
    error,   // Operation error (protocol error, connection failure, etc.)
    timeout, // Timeout (data timeout or request timeout exceeded)
  };

  struct Error
  {
    Failure failure;
    std::string message;

    Error(Failure f, std::string msg = "")
      : failure(f),
        message(std::move(msg))
    {
    }
  };

  struct PutFlags
  {
    bool overwrite = false; // bit 0 (LSB): overwrite existing value
  };

  explicit Client(std::chrono::milliseconds data_timeout,
                  std::chrono::milliseconds request_timeout);
  ~Client();

  tl::expected<void, Error> connect(const std::string& path);
  uint8_t greeting_format() const;
  const std::vector<Capability>& capabilities() const;
  const std::string& server_identity() const;
  const std::vector<std::string>& diagnostics() const;
  bool has_capability(Capability cap) const;

  tl::expected<std::optional<util::Bytes>, Error>
  get(std::span<const uint8_t> key);
  tl::expected<bool, Error> put(std::span<const uint8_t> key,
                                std::span<const uint8_t> value,
                                PutFlags flags);
  tl::expected<bool, Error> remove(std::span<const uint8_t> key);
  tl::expected<void, Error> stop();

  void close();

private:
#ifdef _WIN32
  util::BufferedIpcChannelClient<util::WinNamedPipeClient> m_channel;
#else
  util::BufferedIpcChannelClient<util::UnixSocketClient> m_channel;
#endif
  uint8_t m_greeting_format;
  std::vector<Capability> m_capabilities;
  std::string m_server_identity;
  std::vector<std::string> m_diagnostics;
  bool m_connected = false;
  std::chrono::milliseconds m_data_timeout;
  std::chrono::milliseconds m_request_timeout;
  std::chrono::steady_clock::time_point m_request_start_time;

  std::chrono::milliseconds calculate_timeout() const;

  tl::expected<void, Error> read_greeting();
  tl::expected<void, Error> send_bytes(std::span<const uint8_t> data);
  tl::expected<util::Bytes, Error> receive_bytes(size_t count);
  tl::expected<uint8_t, Error> receive_u8();
  tl::expected<uint64_t, Error> receive_u64();
  tl::expected<std::optional<util::Bytes>, Error> receive_response_get();
  tl::expected<bool, Error> receive_response_bool();
  tl::expected<void, Error> receive_response_void();
};

inline std::string
to_string(Client::Capability capability)
{
  switch (capability) {
  case Client::Capability::get_put_remove_stop:
    return "get_put_remove_stop";
  }
  return FMT("{}", static_cast<int>(capability));
}

} // namespace storage::remote
