// Copyright (C) 2021 Joel Rosdahl and other contributors
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
#include <third_party/nonstd/string_view.hpp>
#include <third_party/url.hpp>

namespace storage {
namespace secondary {

namespace {

class HttpStorageBackend : public SecondaryStorage::Backend
{
public:
  HttpStorageBackend(const Params& params);

  nonstd::expected<nonstd::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  enum class Layout { bazel, standard };

  const std::string m_url_path;
  httplib::Client m_http_client;
  Layout m_layout = Layout::standard;

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
    const auto pair = util::split_once(params.url.user_info(), ':');
    if (!pair.second) {
      throw core::Fatal("Expected username:password in URL but got \"{}\"",
                        params.url.user_info());
    }
    m_http_client.set_basic_auth(std::string(pair.first).c_str(),
                                 std::string(*pair.second).c_str());
  }

  m_http_client.set_default_headers({
    {"User-Agent", FMT("{}/{}", CCACHE_NAME, CCACHE_VERSION)},
  });
  m_http_client.set_keep_alive(true);

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : params.attributes) {
    if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "layout") {
      if (attr.value == "bazel") {
        m_layout = Layout::bazel;
      } else if (attr.value == "standard") {
        m_layout = Layout::standard;
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

nonstd::expected<nonstd::optional<std::string>,
                 SecondaryStorage::Backend::Failure>
HttpStorageBackend::get(const Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client.Get(url_path.c_str());

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to get {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
    return nonstd::make_unexpected(Failure::error);
  }

  if (result->status < 200 || result->status >= 300) {
    // Don't log failure if the entry doesn't exist.
    return nonstd::nullopt;
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
    const auto result = m_http_client.Head(url_path.c_str());

    if (result.error() != httplib::Error::Success || !result) {
      LOG("Failed to check for {} in http storage: {} ({})",
          url_path,
          to_string(result.error()),
          result.error());
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
  const auto result = m_http_client.Put(
    url_path.c_str(), value.data(), value.size(), content_type);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to put {} to http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
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
  const auto result = m_http_client.Delete(url_path.c_str());

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to delete {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
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
    return FMT("{}ac/{}", m_url_path, hex_digits);
  }

  case Layout::standard:
    return m_url_path + key.to_string();
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
  const auto user_info = util::split_once(url.user_info(), ':');
  if (user_info.second) {
    url.user_info(FMT("{}:{}", user_info.first, k_redacted_password));
  }
}

} // namespace secondary
} // namespace storage
