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

#include "HttpStorage.hpp"

#include <Digest.hpp>
#include <Logging.hpp>
#include <ccache.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <third_party/httplib.h>
#include <third_party/url.hpp>

#include <string_view>

namespace storage::secondary {

namespace {

class HttpStorageBackend : public SecondaryStorage::Backend
{
public:
  HttpStorageBackend(const Params& params);

  nonstd::expected<std::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  enum class Layout { bazel, flat, subdirs };

  const std::string m_url_path;
  httplib::Client m_http_client;
  Layout m_layout = Layout::subdirs;

  std::string get_entry_path(const Digest& key) const;
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
    throw core::Fatal("A host is required in HTTP storage URL \"{}\"",
                      url.str());
  }

  // httplib requires a partial URL with just scheme, host and port.
  return get_partial_url(url).str();
}

HttpStorageBackend::HttpStorageBackend(const Params& params)
  : m_url_path(get_url_path(params.url)),
    m_http_client(get_url(params.url))
{
  if (!params.url.user_info().empty()) {
    const auto [user, password] = util::split_once(params.url.user_info(), ':');
    if (!password) {
      throw core::Fatal("Expected username:password in URL but got \"{}\"",
                        params.url.user_info());
    }
    m_http_client.set_basic_auth(std::string(user), std::string(*password));
  }

  m_http_client.set_default_headers({
    {"User-Agent", FMT("ccache/{}", CCACHE_VERSION)},
  });
  m_http_client.set_keep_alive(true);

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : params.attributes) {
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

nonstd::expected<std::optional<std::string>, SecondaryStorage::Backend::Failure>
HttpStorageBackend::get(const Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client.Get(url_path);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to get {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return nonstd::make_unexpected(Failure::error);
  }

  if (result->status < 200 || result->status >= 300) {
    // Don't log failure if the entry doesn't exist.
    return std::nullopt;
  }

  return result->body;
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
HttpStorageBackend::put(const Digest& key,
                        const std::string& value,
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
      return nonstd::make_unexpected(Failure::error);
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
    m_http_client.Put(url_path, value.data(), value.size(), content_type);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to put {} to http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return nonstd::make_unexpected(Failure::error);
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to put {} to http storage: status code: {}",
        url_path,
        result->status);
    return nonstd::make_unexpected(Failure::error);
  }

  return true;
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
HttpStorageBackend::remove(const Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client.Delete(url_path);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to delete {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        static_cast<int>(result.error()));
    return nonstd::make_unexpected(Failure::error);
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to delete {} from http storage: status code: {}",
        url_path,
        result->status);
    return nonstd::make_unexpected(Failure::error);
  }

  return true;
}

std::string
HttpStorageBackend::get_entry_path(const Digest& key) const
{
  switch (m_layout) {
  case Layout::bazel: {
    // Mimic hex representation of a SHA256 hash value.
    const auto sha256_hex_size = 64;
    static_assert(Digest::size() == 20, "Update below if digest size changes");
    std::string hex_digits = Util::format_base16(key.bytes(), key.size());
    hex_digits.append(hex_digits.data(), sha256_hex_size - hex_digits.size());
    LOG("Translated key {} to Bazel layout ac/{}", key.to_string(), hex_digits);
    return FMT("{}ac/{}", m_url_path, hex_digits);
  }

  case Layout::flat:
    return m_url_path + key.to_string();

  case Layout::subdirs: {
    const auto key_str = key.to_string();
    const uint8_t digits = 2;
    ASSERT(key_str.length() > digits);
    return FMT("{}/{:.{}}/{}", m_url_path, key_str, digits, &key_str[digits]);
  }
  }

  ASSERT(false);
}

} // namespace

std::unique_ptr<SecondaryStorage::Backend>
HttpStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<HttpStorageBackend>(params);
}

void
HttpStorage::redact_secrets(Backend::Params& params) const
{
  auto& url = params.url;
  const auto [user, password] = util::split_once(url.user_info(), ':');
  if (password) {
    url.user_info(FMT("{}:{}", user, k_redacted_password));
  }

  auto bearer_token_attribute =
    std::find_if(params.attributes.begin(),
                 params.attributes.end(),
                 [&](const auto& attr) { return attr.key == "bearer-token"; });
  if (bearer_token_attribute != params.attributes.end()) {
    bearer_token_attribute->value = k_redacted_password;
    bearer_token_attribute->raw_value = k_redacted_password;
  }
}

} // namespace storage::secondary
