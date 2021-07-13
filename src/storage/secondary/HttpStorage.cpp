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
#include <Util.hpp>
#include <ccache.hpp>
#include <exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/string_utils.hpp>

#include <third_party/httplib.h>
#include <third_party/nonstd/string_view.hpp>
#include <third_party/url.hpp>

namespace storage {
namespace secondary {

namespace {

const auto DEFAULT_CONNECT_TIMEOUT = std::chrono::milliseconds{100};
const auto DEFAULT_OPERATION_TIMEOUT = std::chrono::milliseconds{10000};

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

std::string
get_host_header_value(const Url& url)
{
  // We need to construct an HTTP Host header that follows the same IPv6
  // escaping rules like a URL. To avoid code duplication we re-use the
  // Url class to render that string.

  Url host_and_port_only;
  host_and_port_only.host(url.host(), url.ip_version()).port(url.port());

  // The rendered_value now contains a string like '//[::1]:8080'. The trailing
  // slashes must be stripped.
  const auto rendered_value = host_and_port_only.str();
  const auto prefix = nonstd::string_view{"//"};
  if (!util::starts_with(rendered_value, prefix)) {
    throw Error(
      "Expected partial URL to start with '{}': '{}'", prefix, rendered_value);
  }
  return rendered_value.substr(prefix.size());
}

std::unique_ptr<httplib::Client>
make_client(const Url& url)
{
  if (url.host().empty()) {
    throw Error("A host is required in HTTP storage URL: '{}'", url.str());
  }

  // the httplib requires a partial URL with just scheme, host and port
  Url destination;
  destination.scheme(url.scheme())
    .host(url.host(), url.ip_version())
    .port(url.port());

  auto client = std::make_unique<httplib::Client>(destination.str().c_str());
  if (!url.user_info().empty()) {
    const auto pair = util::split_once(url.user_info(), ':');
    if (!pair.second) {
      throw Error("Expected username:password in URL but got: '{}'",
                  url.user_info());
    }
    client->set_basic_auth(nonstd::sv_lite::to_string(pair.first).c_str(),
                           nonstd::sv_lite::to_string(*pair.second).c_str());
  }

  return client;
}

std::chrono::milliseconds
parse_timeout_attribute(const AttributeMap& attributes,
                        const std::string& name,
                        const std::chrono::milliseconds default_value)
{
  const auto it = attributes.find(name);
  if (it == attributes.end()) {
    return default_value;
  } else {
    auto timeout_in_ms =
      Util::parse_unsigned(it->second, 1, 1000 * 3600, "timeout");
    return std::chrono::milliseconds{timeout_in_ms};
  }
}

} // namespace

HttpStorage::HttpStorage(const Url& url, const AttributeMap& attributes)
  : m_url_path(get_url_path(url)),
    m_http_client(make_client(url))
{
  m_http_client->set_default_headers(
    {// explicit setting of the Host header is required due to IPv6 address
     // handling issues in httplib
     {"Host", get_host_header_value(url)},
     {"User-Agent", FMT("{}/{}", CCACHE_NAME, CCACHE_VERSION)}});
  m_http_client->set_keep_alive(true);
  configure_timeouts(attributes);
}

HttpStorage::~HttpStorage() = default;

void
HttpStorage::configure_timeouts(const AttributeMap& attributes)
{
  const auto connection_timeout = parse_timeout_attribute(
    attributes, "connect-timeout", DEFAULT_CONNECT_TIMEOUT);
  const auto operation_timeout = parse_timeout_attribute(
    attributes, "operation-timeout", DEFAULT_OPERATION_TIMEOUT);

  m_http_client->set_connection_timeout(connection_timeout);
  m_http_client->set_read_timeout(operation_timeout);
  m_http_client->set_write_timeout(operation_timeout);
}

nonstd::expected<nonstd::optional<std::string>, SecondaryStorage::Error>
HttpStorage::get(const Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client->Get(url_path.c_str());

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to get {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
    return nonstd::make_unexpected(Error::error);
  }

  if (result->status < 200 || result->status >= 300) {
    // Don't log failure if the entry doesn't exist.
    return nonstd::nullopt;
  }

  return result->body;
}

nonstd::expected<bool, SecondaryStorage::Error>
HttpStorage::put(const Digest& key,
                 const std::string& value,
                 const bool only_if_missing)
{
  const auto url_path = get_entry_path(key);

  if (only_if_missing) {
    const auto result = m_http_client->Head(url_path.c_str());

    if (result.error() != httplib::Error::Success || !result) {
      LOG("Failed to check for {} in http storage: {} ({})",
          url_path,
          to_string(result.error()),
          result.error());
      return nonstd::make_unexpected(Error::error);
    }

    if (result->status >= 200 && result->status < 300) {
      LOG("Found entry {} already within http storage: status code: {}",
          url_path,
          result->status);
      return false;
    }
  }

  static const auto content_type = "application/octet-stream";
  const auto result = m_http_client->Put(
    url_path.c_str(), value.data(), value.size(), content_type);

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to put {} to http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
    return nonstd::make_unexpected(Error::error);
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to put {} to http storage: status code: {}",
        url_path,
        result->status);
    return nonstd::make_unexpected(Error::error);
  }

  return true;
}

nonstd::expected<bool, SecondaryStorage::Error>
HttpStorage::remove(const Digest& key)
{
  const auto url_path = get_entry_path(key);
  const auto result = m_http_client->Delete(url_path.c_str());

  if (result.error() != httplib::Error::Success || !result) {
    LOG("Failed to delete {} from http storage: {} ({})",
        url_path,
        to_string(result.error()),
        result.error());
    return nonstd::make_unexpected(Error::error);
  }

  if (result->status < 200 || result->status >= 300) {
    LOG("Failed to delete {} from http storage: status code: {}",
        url_path,
        result->status);
    return nonstd::make_unexpected(Error::error);
  }

  return true;
}

std::string
HttpStorage::get_entry_path(const Digest& key) const
{
  return m_url_path + key.to_string();
}

} // namespace secondary
} // namespace storage
