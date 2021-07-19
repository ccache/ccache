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
  const std::string m_url_path;
  httplib::Client m_http_client;

  std::string get_entry_path(const Digest& key) const;
};

nonstd::string_view
to_string(const httplib::Error error)
{
  using httplib::Error;

  switch (error) {
  case Error::Success:
    return "Success";
  case Error::Connection:
    return "Connection";
  case Error::BindIPAddress:
    return "BindIPAddress";
  case Error::Read:
    return "Read";
  case Error::Write:
    return "Write";
  case Error::ExceedRedirectCount:
    return "ExceedRedirectCount";
  case Error::Canceled:
    return "Canceled";
  case Error::SSLConnection:
    return "SSLConnection";
  case Error::SSLLoadingCerts:
    return "SSLLoadingCerts";
  case Error::SSLServerVerification:
    return "SSLServerVerification";
  case Error::UnsupportedMultipartBoundaryChars:
    return "UnsupportedMultipartBoundaryChars";
  case Error::Compression:
    return "Compression";
  case Error::Unknown:
    break;
  }

  return "Unknown";
}

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
  url.host(from_url.host(), from_url.ip_version());
  if (!from_url.port().empty()) {
    url.port(from_url.port());
  }
  return url;
}

std::string
get_host_header_value(const Url& url)
{
  // We need to construct an HTTP Host header that follows the same IPv6
  // escaping rules like a URL.
  const auto rendered_value = get_partial_url(url).str();

  // The rendered_value now contains a string like "//[::1]:8080". The leading
  // slashes must be stripped.
  const auto prefix = nonstd::string_view{"//"};
  if (!util::starts_with(rendered_value, prefix)) {
    throw core::Fatal(R"(Expected partial URL "{}" to start with "{}")",
                      rendered_value,
                      prefix);
  }
  return rendered_value.substr(prefix.size());
}

std::string
get_url(const Url& url)
{
  if (url.host().empty()) {
    throw core::Fatal("A host is required in HTTP storage URL \"{}\"",
                      url.str());
  }

  // httplib requires a partial URL with just scheme, host and port.
  return get_partial_url(url).scheme(url.scheme()).str();
}

HttpStorageBackend::HttpStorageBackend(const Params& params)
  : m_url_path(get_url_path(params.url)),
    m_http_client(get_url(params.url).c_str())
{
  if (!params.url.user_info().empty()) {
    const auto pair = util::split_once(params.url.user_info(), ':');
    if (!pair.second) {
      throw core::Fatal("Expected username:password in URL but got \"{}\"",
                        params.url.user_info());
    }
    m_http_client.set_basic_auth(to_string(pair.first).c_str(),
                                 to_string(*pair.second).c_str());
  }

  m_http_client.set_default_headers({
    // Explicit setting of the Host header is required due to IPv6 address
    // handling issues in httplib.
    {"Host", get_host_header_value(params.url)},
    {"User-Agent", FMT("{}/{}", CCACHE_NAME, CCACHE_VERSION)},
  });
  m_http_client.set_keep_alive(true);

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
  return m_url_path + key.to_string();
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
