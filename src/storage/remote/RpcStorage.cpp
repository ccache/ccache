// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
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

#include "RpcStorage.hpp"

#include "RpcStorage_msgpack.hpp"

#include <Digest.hpp>
#include <Logging.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <rpc/client.h>
#include <rpc/rpc_error.h>

#include <cstdarg>
#include <map>
#include <memory>

namespace storage::remote {

namespace {

const uint32_t DEFAULT_PORT = 8080;

class RpcStorageBackend : public RemoteStorage::Backend
{
public:
  RpcStorageBackend(const RemoteStorage::Backend::Params& params);

  nonstd::expected<std::optional<util::Bytes>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      nonstd::span<const uint8_t> value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  rpc::client* m_rpc_client;

  void
  connect(const Url& url, uint32_t connect_timeout, uint32_t operation_timeout);
};

RpcStorageBackend::RpcStorageBackend(const Params& params)
  : m_rpc_client(nullptr)
{
  const auto& url = params.url;
  ASSERT(url.scheme() == "rpc");

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : params.attributes) {
    if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

  connect(url, connect_timeout.count(), operation_timeout.count());
}

nonstd::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
RpcStorageBackend::get(const Digest& key)
{
  LOG("RPC get {}", key.to_string());
  try {
    auto reply = m_rpc_client->call("get", key).as<util::Bytes>();
    if (reply.size() == 0) {
      return std::nullopt;
    }
    return reply;
  } catch (rpc::timeout&) {
    return nonstd::make_unexpected(Failure::timeout);
  } catch (std::runtime_error& e) {
    LOG("RPC error: {}", e.what());
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, RemoteStorage::Backend::Failure>
RpcStorageBackend::put(const Digest& key,
                       nonstd::span<const uint8_t> value,
                       bool only_if_missing)
{
  if (only_if_missing) {
    LOG("RPC exists {}", key.to_string());
    try {
      if (m_rpc_client->call("exists", key).as<bool>()) {
        return false;
      }
    } catch (rpc::timeout&) {
      return nonstd::make_unexpected(Failure::timeout);
    } catch (std::runtime_error& e) {
      LOG("RPC error: {}", e.what());
      return nonstd::make_unexpected(Failure::error);
    }
  }
  LOG("RPC put {} [{} bytes]", key.to_string(), value.size());
  try {
    return m_rpc_client->call("put", key, value).as<bool>();
  } catch (rpc::timeout&) {
    return nonstd::make_unexpected(Failure::timeout);
  } catch (std::runtime_error& e) {
    LOG("RPC error: {}", e.what());
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, RemoteStorage::Backend::Failure>
RpcStorageBackend::remove(const Digest& key)
{
  LOG("RPC remove {}", key.to_string());
  try {
    return m_rpc_client->call("remove", key).as<bool>();
  } catch (rpc::timeout&) {
    return nonstd::make_unexpected(Failure::timeout);
  } catch (std::runtime_error& e) {
    LOG("RPC error: {}", e.what());
    return nonstd::make_unexpected(Failure::error);
  }
}

void
RpcStorageBackend::connect(const Url& url,
                           const uint32_t connect_timeout,
                           const uint32_t operation_timeout)
{
  const std::string host = url.host().empty() ? "localhost" : url.host();
  const uint32_t port = url.port().empty()
                          ? DEFAULT_PORT
                          : util::value_or_throw<core::Fatal>(
                            util::parse_unsigned(url.port(), 1, 65535, "port"));
  ASSERT(url.path().empty() || url.path()[0] == '/');

  LOG("RPC connecting to {}:{} (connect timeout {} ms)",
      host,
      port,
      connect_timeout);
  try {
    // TODO: connect_timeout
    m_rpc_client = new rpc::client(host, port);
  } catch (rpc::system_error& e) {
    throw Failed(FMT("RPC client construction error: {}", e.what()));
  }

  LOG("RPC operation timeout set to {} ms", operation_timeout);
  m_rpc_client->set_timeout(operation_timeout);
}

} // namespace

std::unique_ptr<RemoteStorage::Backend>
RpcStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<RpcStorageBackend>(params);
}

} // namespace storage::remote
