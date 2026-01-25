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

#include "client.hpp"

#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/format.hpp>

#include <algorithm>
#include <cstring>

namespace storage::remote {

namespace {

constexpr uint8_t k_request_get = 0x00;
constexpr uint8_t k_request_put = 0x01;
constexpr uint8_t k_request_remove = 0x02;
constexpr uint8_t k_request_stop = 0x03;

} // namespace

static Client::Error
make_error(const util::IpcError& ipc_error)
{
  auto failure = (ipc_error.failure == util::IpcError::Failure::timeout)
                   ? Client::Failure::timeout
                   : Client::Failure::error;
  return Client::Error(failure, ipc_error.message);
}

Client::Client(std::chrono::milliseconds data_timeout,
               std::chrono::milliseconds request_timeout)
  : m_data_timeout(data_timeout),
    m_request_timeout(request_timeout)
{
}

Client::~Client()
{
  close();
}

std::chrono::milliseconds
Client::calculate_timeout() const
{
  // Calculate remaining time for current request
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_request_start_time);
  auto remaining_request_timeout = m_request_timeout - elapsed;

  if (remaining_request_timeout <= std::chrono::milliseconds(0)) {
    // Already expired.
    return std::chrono::milliseconds(0);
  }

  return std::min(m_data_timeout, remaining_request_timeout);
}

tl::expected<void, Client::Error>
Client::connect(const std::string& path)
{
  if (m_connected) {
    return tl::unexpected(Error(Failure::error, "Already connected"));
  }

  m_request_start_time = std::chrono::steady_clock::now();

  auto timeout = calculate_timeout();
  auto result = m_channel.connect(path, timeout);
  if (!result) {
    return tl::unexpected(make_error(result.error()));
  }

  TRY(read_greeting());

  m_connected = true;
  return {};
}

uint8_t
Client::protocol_version() const
{
  return m_protocol_version;
}

const std::vector<Client::Capability>&
Client::capabilities() const
{
  return m_capabilities;
}

bool
Client::has_capability(Capability cap) const
{
  return std::find(m_capabilities.begin(), m_capabilities.end(), cap)
         != m_capabilities.end();
}

tl::expected<std::optional<util::Bytes>, Client::Error>
Client::get(nonstd::span<const uint8_t> key)
{
  if (!m_connected) {
    return tl::unexpected(Error(Failure::error, "Not connected"));
  }

  if (key.size() > 255) {
    return tl::unexpected(
      Error(Failure::error, "Key too long (max 255 bytes)"));
  }

  m_request_start_time = std::chrono::steady_clock::now();

  TRY(send_u8(k_request_get));
  TRY(send_key(key));

  return receive_response_get();
}

tl::expected<bool, Client::Error>
Client::put(nonstd::span<const uint8_t> key,
            nonstd::span<const uint8_t> value,
            PutFlags flags)
{
  if (!m_connected) {
    return tl::unexpected(Error(Failure::error, "Not connected"));
  }

  if (key.size() > 255) {
    return tl::unexpected(
      Error(Failure::error, "Key too long (max 255 bytes)"));
  }

  m_request_start_time = std::chrono::steady_clock::now();

  uint8_t flag_byte = flags.overwrite ? 0x01 : 0x00;
  TRY(send_u8(k_request_put));
  TRY(send_key(key));
  TRY(send_u8(flag_byte));
  TRY(send_value(value));
  return receive_response_bool();
}

tl::expected<bool, Client::Error>
Client::remove(nonstd::span<const uint8_t> key)
{
  if (!m_connected) {
    return tl::unexpected(Error(Failure::error, "Not connected"));
  }

  if (key.size() > 255) {
    return tl::unexpected(
      Error(Failure::error, "Key too long (max 255 bytes)"));
  }

  m_request_start_time = std::chrono::steady_clock::now();

  TRY(send_u8(k_request_remove));
  TRY(send_key(key));
  return receive_response_bool();
}

tl::expected<void, Client::Error>
Client::stop()
{
  if (!m_connected) {
    return tl::unexpected(Error(Failure::error, "Not connected"));
  }

  m_request_start_time = std::chrono::steady_clock::now();

  TRY(send_u8(k_request_stop));
  return receive_response_void();
}

void
Client::close()
{
  if (m_connected) {
    m_channel.close();
    m_connected = false;
    m_protocol_version = 0;
    m_capabilities.clear();
  }
}

tl::expected<void, Client::Error>
Client::read_greeting()
{
  TRY_ASSIGN(m_protocol_version, receive_u8());
  if (m_protocol_version != k_protocol_version) {
    return tl::unexpected(
      Error(Failure::error,
            FMT("Unsupported protocol version: {}", m_protocol_version)));
  }

  TRY_ASSIGN(uint8_t cap_len, receive_u8());
  m_capabilities.clear();
  m_capabilities.reserve(cap_len);
  for (uint8_t i = 0; i < cap_len; ++i) {
    TRY_ASSIGN(uint8_t cap_byte, receive_u8());
    m_capabilities.push_back(static_cast<Capability>(cap_byte));
  }

  return {};
}

tl::expected<void, Client::Error>
Client::send_bytes(nonstd::span<const uint8_t> data)
{
  auto timeout = calculate_timeout();
  auto result = m_channel.send(data, timeout);
  if (!result) {
    return tl::unexpected(make_error(result.error()));
  }
  return {};
}

tl::expected<util::Bytes, Client::Error>
Client::receive_bytes(size_t count)
{
  util::Bytes result(count);
  size_t total_received = 0;

  while (total_received < count) {
    nonstd::span<uint8_t> buffer(result.data() + total_received,
                                 count - total_received);
    auto timeout = calculate_timeout();
    auto recv_result = m_channel.receive(buffer, timeout);
    if (!recv_result) {
      return tl::unexpected(make_error(recv_result.error()));
    }

    if (*recv_result == 0) {
      return tl::unexpected(
        Error(Failure::error, "Connection closed by server"));
    }

    total_received += *recv_result;
  }

  return result;
}

tl::expected<uint8_t, Client::Error>
Client::receive_u8()
{
  TRY_ASSIGN(auto data, receive_bytes(sizeof(uint8_t)));
  return data[0];
}

tl::expected<uint64_t, Client::Error>
Client::receive_u64()
{
  TRY_ASSIGN(auto data, receive_bytes(sizeof(uint64_t)));
  uint64_t value;
  std::memcpy(&value, data.data(), sizeof(uint64_t)); // host byte order
  return value;
}

tl::expected<void, Client::Error>
Client::send_u8(uint8_t value)
{
  return send_bytes({&value, 1});
}

tl::expected<void, Client::Error>
Client::send_u64(uint64_t value)
{
  uint8_t buffer[sizeof(uint64_t)];
  std::memcpy(buffer, &value, sizeof(uint64_t)); // host byte order
  return send_bytes(buffer);
}

tl::expected<void, Client::Error>
Client::send_key(nonstd::span<const uint8_t> key)
{
  DEBUG_ASSERT(key.size() < 256);
  auto key_len = static_cast<uint8_t>(key.size());
  TRY(send_u8(key_len));
  TRY(send_bytes(key));
  return {};
}

tl::expected<void, Client::Error>
Client::send_value(nonstd::span<const uint8_t> value)
{
  TRY(send_u64(value.size()));
  TRY(send_bytes(value));
  return {};
}

tl::expected<std::optional<util::Bytes>, Client::Error>
Client::receive_response_get()
{
  TRY_ASSIGN(uint8_t status_byte, receive_u8());
  auto status = static_cast<Status>(status_byte);

  switch (status) {
  case Status::ok: {
    TRY_ASSIGN(uint64_t value_len, receive_u64());
    TRY_ASSIGN(auto value, receive_bytes(value_len));
    return value;
  }

  case Status::noop: // key not found
    return std::nullopt;

  case Status::error: {
    TRY_ASSIGN(uint8_t msg_len, receive_u8());
    TRY_ASSIGN(auto msg_bytes, receive_bytes(msg_len));
    std::string error_msg(msg_bytes.begin(), msg_bytes.end());
    return tl::unexpected(Error(Failure::error, error_msg));
  }

  default:
    return tl::unexpected(
      Error(Failure::error, FMT("Invalid status code: {}", status_byte)));
  }
}

tl::expected<bool, Client::Error>
Client::receive_response_bool()
{
  TRY_ASSIGN(uint8_t status_byte, receive_u8());
  auto status = static_cast<Status>(status_byte);

  switch (status) {
  case Status::ok:
    return true;

  case Status::noop:
    return false;

  case Status::error: {
    TRY_ASSIGN(uint8_t msg_len, receive_u8());
    TRY_ASSIGN(auto msg_bytes, receive_bytes(msg_len));
    std::string error_msg(msg_bytes.begin(), msg_bytes.end());
    return tl::unexpected(Error(Failure::error, error_msg));
  }

  default:
    return tl::unexpected(
      Error(Failure::error, FMT("Invalid status code: {}", status_byte)));
  }
}

tl::expected<void, Client::Error>
Client::receive_response_void()
{
  TRY_ASSIGN(uint8_t status_byte, receive_u8());
  auto status = static_cast<Status>(status_byte);

  switch (status) {
  case Status::ok:
    return {};

  case Status::noop:
    // This shouldn't happen for stop, but treat it as success.
    return {};

  case Status::error: {
    TRY_ASSIGN(uint8_t msg_len, receive_u8());
    TRY_ASSIGN(auto msg_bytes, receive_bytes(msg_len));
    std::string error_msg(msg_bytes.begin(), msg_bytes.end());
    return tl::unexpected(Error(Failure::error, error_msg));
  }

  default:
    return tl::unexpected(
      Error(Failure::error, FMT("Invalid status code: {}", status_byte)));
  }
}

} // namespace storage::remote
