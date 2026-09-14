// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "ocistorage.hpp"

#include <ccache/ccache.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>

#include <cxxurl/url.hpp>
#include <httplib.h>

#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace storage::remote {

namespace {

struct OciConfig
{
  std::string registry;
  std::string repository;
  std::string prefix;
  std::string token;
  bool debug = false;
  std::chrono::milliseconds connect_timeout = k_default_connect_timeout;
  std::chrono::milliseconds operation_timeout = k_default_operation_timeout;
};

std::optional<std::string>
getenv_string(const char* name)
{
  const char* value = std::getenv(name);
  if (value && *value) {
    return value;
  }
  return std::nullopt;
}

bool
parse_bool(std::string_view value)
{
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string
strip_slashes(std::string value)
{
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string
object_key(const Hash::Digest& key, const std::string& prefix)
{
  const std::string digest = util::format_base16(key);
  return prefix.empty() ? digest : FMT("{}/{}", prefix, digest);
}

std::string
api_path(const std::string& repository, const std::string& key)
{
  return FMT("/v2/{}/ccache/blobs/{}", repository, key);
}

void
log_once(std::set<std::string>& seen,
         const std::string& code,
         const std::string& message)
{
  if (seen.insert(code).second) {
    LOG("{}: {}", code, message);
  }
}

RemoteStorage::Backend::Failure
failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

OciConfig
parse_config(const Url& url,
             const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  if (url.host().empty()) {
    throw core::Fatal(FMT(
      "CCACHE-OCI-0007: registry host is required in OCI storage URL \"{}\"",
      storage::get_redacted_url_str_for_logging(url)));
  }

  OciConfig config;
  config.registry = url.host();
  if (!url.port().empty()) {
    config.registry += FMT(":{}", url.port());
  }

  std::string path = strip_slashes(url.path());
  const auto prefix_pos = path.find("/ccache/");
  if (prefix_pos != std::string::npos) {
    config.repository = path.substr(0, prefix_pos);
    config.prefix = strip_slashes(path.substr(prefix_pos + 8));
  } else {
    config.repository = path;
  }
  if (config.repository.empty()) {
    throw core::Fatal(FMT(
      "CCACHE-OCI-0008: repository path is required in OCI storage URL \"{}\"",
      storage::get_redacted_url_str_for_logging(url)));
  }

  for (const auto& attr : attributes) {
    if (attr.key == "token") {
      config.token = attr.value;
    } else if (attr.key == "token-env") {
      config.token = getenv_string(attr.value.c_str()).value_or("");
    } else if (attr.key == "prefix") {
      config.prefix = strip_slashes(attr.value);
    } else if (attr.key == "debug") {
      config.debug = parse_bool(attr.value);
    } else if (attr.key == "connect-timeout") {
      config.connect_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      config.operation_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else {
      LOG("CCACHE-OCI-0009: unknown OCI storage attribute: {}", attr.key);
    }
  }

  return config;
}

class OciStorageBackend : public RemoteStorage::Backend
{
public:
  OciStorageBackend(const Url& url,
                    const std::vector<Backend::Attribute>& attributes)
    : m_config(parse_config(url, attributes)),
      m_redacted_url(storage::get_redacted_url_str_for_logging(url)),
      m_http_client(FMT("https://{}", m_config.registry))
  {
    httplib::Headers headers;
    headers.emplace("User-Agent", FMT("ccache/{}", CCACHE_VERSION));
    if (!m_config.token.empty()) {
      headers.emplace("Authorization", FMT("Bearer {}", m_config.token));
    }
    m_http_client.set_keep_alive(true);
    m_http_client.set_connection_timeout(m_config.connect_timeout);
    m_http_client.set_read_timeout(m_config.operation_timeout);
    m_http_client.set_write_timeout(m_config.operation_timeout);
    m_http_client.set_default_headers(headers);
  }

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override
  {
    const std::string entry_key = object_key(key, m_config.prefix);
    const std::string path = api_path(m_config.repository, entry_key);
    const auto result = m_http_client.Get(path);
    if (!result || result.error() != httplib::Error::Success) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0001",
               FMT("failed to get entry from OCI storage {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE-OCI-DEBUG: GET {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status == 404) {
      return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0002",
               FMT("OCI storage returned status {}", result->status));
      return tl::unexpected(Failure::error);
    }
    return util::Bytes(result->body.data(), result->body.size());
  }

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  std::span<const uint8_t> value,
                                  Overwrite overwrite) override
  {
    const std::string entry_key = object_key(key, m_config.prefix);
    const std::string path = api_path(m_config.repository, entry_key);
    if (overwrite == Overwrite::no) {
      const auto head = m_http_client.Head(path);
      if (head && head->status >= 200 && head->status < 300) {
        return false;
      }
    }

    const auto result =
      m_http_client.Put(path,
                        reinterpret_cast<const char*>(value.data()),
                        value.size(),
                        "application/vnd.ccache.entry");
    if (!result || result.error() != httplib::Error::Success) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0003",
               FMT("failed to put entry to OCI storage {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE-OCI-DEBUG: PUT {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status < 200 || result->status >= 300) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0004",
               FMT("OCI storage rejected upload with status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override
  {
    const std::string entry_key = object_key(key, m_config.prefix);
    const std::string path = api_path(m_config.repository, entry_key);
    const auto result = m_http_client.Delete(path);
    if (!result || result.error() != httplib::Error::Success) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0005",
               FMT("failed to delete entry from OCI storage {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE-OCI-DEBUG: DELETE {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status == 404) {
      return false;
    }
    if (result->status < 200 || result->status >= 300) {
      log_once(m_seen_errors,
               "CCACHE-OCI-0006",
               FMT("OCI storage rejected delete with status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

private:
  OciConfig m_config;
  std::string m_redacted_url;
  httplib::Client m_http_client;
  std::set<std::string> m_seen_errors;
};

} // namespace

std::unique_ptr<RemoteStorage::Backend>
OciStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<OciStorageBackend>(url, attributes);
}

} // namespace storage::remote
