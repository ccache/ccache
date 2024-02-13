// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include "HttpStorage.hpp"

#include <Hash.hpp>
#include <ccache.hpp>
#include <core/exceptions.hpp>
#include <storage/Storage.hpp>
#include <util/assertions.hpp>
#include <util/expected.hpp>
#include <util/fmtmacros.hpp>
#include <util/logging.hpp>
#include <util/string.hpp>
#include <util/types.hpp>

#include <third_party/url.hpp>

#include <httplib.h>

#include <string_view>

namespace storage::remote {

namespace {

class HttpStorageBackend : public RemoteStorage::Backend
{
public:
  HttpStorageBackend(const Url& url,
                     const std::vector<Backend::Attribute>& attributes);

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  nonstd::span<const uint8_t> value,
                                  bool only_if_missing) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

private:
  enum class Layout { bazel, flat, subdirs };

  const std::string m_url_path;
  httplib::Client m_http_client;
  Layout m_layout = Layout::subdirs;

  std::string get_entry_path(const Hash::Digest& key) const;
};

std::string
get_url_path(const Url& url)
{
  auto path = url.path();
  if (path.empty() || path.back() != '/') {
    path += '/';
  }
  return path;
}

Url
get_partial_url(const Url& from_url)
{
  Url url;
  url.scheme(from_url.scheme());
  url.host(from_url.host(), from_url.ip_version());
  if (!from_url.port().empty()) {
    url.port(from_url.port());
  }
  return url;
}

std::string
get_url(const Url& url)
{
  if (url.host().empty()) {
    throw core::Fatal(
      FMT("A host is required in HTTP storage URL \"{}\"", url.str()));
  }

  // httplib requires a partial URL with just scheme, host and port.
  return get_partial_url(url).str();
}

RemoteStorage::Backend::Failure
failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

HttpStorageBackend::HttpStorageBackend(
  const Url& url, const std::vector<Backend::Attribute>& attributes)
  : m_url_path(get_url_path(url)),
    m_http_client(get_url(url))
{
  if (!url.user_info().empty()) {
    const auto [user, password] = util::split_once(url.user_info(), ':');
    if (!password) {
      throw core::Fatal(FMT("Expected username:password in URL but got \"{}\"",
                            url.user_info()));
    }
    m_http_client.set_basic_auth(std::string(user), std::string(*password));
  }

  m_http_client.set_default_headers({
    {"User-Agent", FMT("ccache/{}", CCACHE_VERSION)},
  });
  m_http_client.set_keep_alive(true);

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : attributes) {
    if (attr.key == "bearer-token") {
      m_http_client.set_bearer_token_auth(attr.value);
    } else if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "keep-alive") {
      m_http_client.set_keep_alive(attr.value == "true");
    } else if (attr.key == "layout") {
      if (attr.value == "bazel") {
        m_layout = Layout::bazel;
      } else if (attr.value == "flat") {
        m_layout = Layout::flat;
      } else if (attr.value == "subdirs") {
        m_layout = Layout::subdirs;
      } else {
        LOG("Unknown layout: {}", attr.value);
      }
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

  m_http_client.set_connection_timeout(connect_timeout);
  m_http_client.set_read_timeout(operation_timeout);
  m_http_client.set_write_timeout(operation_timeout);
}

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
HttpStorageBackend::get(const Hash::Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client.Get(url_path);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to get {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return tl::unexpected(failure_from_httplib_error(result.error()));
  }

  if (result->status < 200 || result->status >= 300) {
    // Don't log failure if the entry doesn't exist.
    return std::nullopt;
  }

  return util::Bytes(result->body.data(), result->body.size());
}

tl::expected<bool, RemoteStorage::Backend::Failure>
HttpStorageBackend::put(const Hash::Digest& key,
                        const nonstd::span<const uint8_t> value,
                        const bool only_if_missing)
{
  const auto url_path = get_entry_path(key);

  if (only_if_missing) {
    const auto result = m_http_client.Head(url_path);

    if (result.error() != httplib::Error::Success || !result) {
      LOG("Failed to check for {} in http storage: {} ({})",
          url_path,
          to_string(result.error()),
          static_cast<int>(result.error()));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }

    if (result->status >= 200 && result->status < 300) {
      LOG("Found entry {} already within http storage: status code: {}",
          url_path,
          result->status);
      return false;
    }
  }

  static const auto content_type = "application/octet-stream";
  const auto result =
    m_http_client.Put(url_path,
                      reinterpret_cast<const char*>(value.data()),
                      value.size(),
                      content_type);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to put {} to http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return tl::unexpected(failure_from_httplib_error(result.error()));
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to put {} to http storage: status code: {}",
        url_path,
        result->status);
    return tl::unexpected(failure_from_httplib_error(result.error()));
  }

  return true;
}

tl::expected<bool, RemoteStorage::Backend::Failure>
HttpStorageBackend::remove(const Hash::Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client.Delete(url_path);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to delete {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return tl::unexpected(failure_from_httplib_error(result.error()));
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to delete {} from http storage: status code: {}",
        url_path,
        result->status);
    return tl::unexpected(failure_from_httplib_error(result.error()));
  }

  return true;
}

std::string
HttpStorageBackend::get_entry_path(const Hash::Digest& key) const
{
  switch (m_layout) {
  case Layout::bazel: {
    // Mimic hex representation of a SHA256 hash value.
    const auto sha256_hex_size = 64;
    static_assert(std::tuple_size<Hash::Digest>() == 20,
                  "Update below if digest size changes");
    std::string hex_digits = util::format_base16(key);
    hex_digits.append(hex_digits.data(), sha256_hex_size - hex_digits.size());
    LOG("Translated key {} to Bazel layout ac/{}",
        util::format_digest(key),
        hex_digits);
    return FMT("{}ac/{}", m_url_path, hex_digits);
  }

  case Layout::flat:
    return m_url_path + util::format_digest(key);

  case Layout::subdirs: {
    const auto key_str = util::format_digest(key);
    const uint8_t digits = 2;
    ASSERT(key_str.length() > digits);
    return FMT("{}{:.{}}/{}", m_url_path, key_str, digits, &key_str[digits]);
  }
  }

  ASSERT(false);
}

} // namespace

std::unique_ptr<RemoteStorage::Backend>
HttpStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<HttpStorageBackend>(url, attributes);
}

void
HttpStorage::redact_secrets(std::vector<Backend::Attribute>& attributes) const
{
  auto bearer_token_attribute =
    std::find_if(attributes.begin(), attributes.end(), [&](const auto& attr) {
      return attr.key == "bearer-token";
    });
  if (bearer_token_attribute != attributes.end()) {
    bearer_token_attribute->value = storage::k_redacted_password;
    bearer_token_attribute->raw_value = storage::k_redacted_password;
  }
}

} // namespace storage::remote
