// Copyright (C) 2022 Joel Rosdahl and other contributors
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

#include "TkrzwStorage.hpp"

#include <Digest.hpp>
#include <Logging.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <tkrzw_dbm_poly.h>
#include <tkrzw_dbm_remote.h>

namespace storage {
namespace secondary {

namespace {

const uint32_t DEFAULT_PORT = 1978;

class TkrzwStorageBackend : public SecondaryStorage::Backend
{
public:
  TkrzwStorageBackend(const SecondaryStorage::Backend::Params& params);
  ~TkrzwStorageBackend() override;

  nonstd::expected<nonstd::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  bool is_local;
  tkrzw::PolyDBM* m_local;
  tkrzw::RemoteDBM* m_remote;

  inline tkrzw::Status
  Get(std::string_view key, std::string* value = nullptr)
  {
    if (is_local) {
      return m_local->Get(key, value);
    } else {
      return m_remote->Get(key, value);
    }
  }
  inline tkrzw::Status
  Set(std::string_view key, std::string_view value, bool overwrite = true)
  {
    if (is_local) {
      return m_local->Set(key, value, overwrite);
    } else {
      return m_remote->Set(key, value, overwrite);
    }
  }
  inline tkrzw::Status
  Remove(std::string_view key)
  {
    if (is_local) {
      return m_local->Remove(key);
    } else {
      return m_remote->Remove(key);
    }
  }

  void
  connect(const Url& url, uint32_t connect_timeout, uint32_t operation_timeout);
  std::string get_key_string(const Digest& digest) const;
};

TkrzwStorageBackend::TkrzwStorageBackend(const Params& params)
  : m_local(nullptr),
    m_remote(nullptr)
{
  const auto& url = params.url;
  ASSERT(url.scheme() == "tkrzw" || (url.scheme() == "tkrzw+unix"));

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

TkrzwStorageBackend::~TkrzwStorageBackend()
{
  if (is_local) {
    if (m_local) {
      m_local->Close();
      delete m_local;
      m_local = nullptr;
    }
  } else {
    if (m_remote) {
      m_remote->Disconnect();
      delete m_remote;
      m_remote = nullptr;
    }
  }
}

nonstd::expected<nonstd::optional<std::string>,
                 SecondaryStorage::Backend::Failure>
TkrzwStorageBackend::get(const Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Tkrzw Get {}", key_string);
  std::string value;
  const auto status = Get(key_string, &value);
  if (status == tkrzw::Status::SUCCESS) {
    return value;
  } else if (status == tkrzw::Status::NOT_FOUND_ERROR) {
    return nonstd::nullopt;
  } else {
    LOG_RAW(std::string(status));
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
TkrzwStorageBackend::put(const Digest& key,
                         const std::string& value,
                         bool only_if_missing)
{
  const auto key_string = get_key_string(key);
  LOG("Tkrzw Set {} [{} bytes]", key_string, value.size());
  const auto status = Set(key_string, value, !only_if_missing);
  if (status == tkrzw::Status::SUCCESS) {
    return true;
  } else {
    LOG_RAW(std::string(status));
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
TkrzwStorageBackend::remove(const Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Tkrzw Remove {}", key_string);
  const auto status = Remove(key_string);
  if (status == tkrzw::Status::SUCCESS) {
    return true;
  } else {
    LOG_RAW(std::string(status));
    return nonstd::make_unexpected(Failure::error);
  }
}

void
TkrzwStorageBackend::connect(const Url& url,
                             const uint32_t connect_timeout,
                             const uint32_t operation_timeout)
{
  const bool unix = (url.scheme() == "tkrzw+unix");
  const std::string host = url.host().empty() ? "localhost" : url.host();
  const uint32_t port = url.port().empty()
                          ? DEFAULT_PORT
                          : util::value_or_throw<core::Fatal>(
                            util::parse_unsigned(url.port(), 1, 65535, "port"));
  ASSERT(url.path().empty() || url.path()[0] == '/');

  if (url.host().empty() && !unix) {
    LOG("Tkrzw opening dbm {}", url.path());
    is_local = true;
    m_local = new tkrzw::PolyDBM();
    const auto status = m_local->Open(url.path(), true);
    if (status != tkrzw::Status::SUCCESS) {
      throw Failed(FMT("Tkrzw open error: {}", std::string(status)));
    }
    LOG_RAW("Tkrzw open local OK");
  } else {
    std::string address;
    if (!unix) {
      address = FMT("{}:{}", host, port);
    } else {
      address = FMT("unix:{}", url.path());
    }
    LOG(
      "Tkrzw connecting to {} (connect timeout {} ms, operation_timeout {} "
      "ms)",
      address,
      connect_timeout,
      operation_timeout);
    is_local = false;
    m_remote = new tkrzw::RemoteDBM();
    // NOTE: currently the _same_ timeout value is used,
    // for both connection and database operation <sigh>
    const auto status =
      m_remote->Connect(address, operation_timeout / 1000.0);
    if (status != tkrzw::Status::SUCCESS) {
      throw Failed(FMT("Tkrzw connect error: {}", std::string(status)));
    }
    LOG_RAW("Tkrzw connect remote OK");
  }
}

std::string
TkrzwStorageBackend::get_key_string(const Digest& digest) const
{
  return digest.to_string();
}

} // namespace

std::unique_ptr<SecondaryStorage::Backend>
TkrzwStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<TkrzwStorageBackend>(params);
}

} // namespace secondary
} // namespace storage
