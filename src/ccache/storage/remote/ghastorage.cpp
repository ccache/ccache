// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "ghastorage.hpp"

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

std::optional<std::string>
getenv_string(const char* name)
{
  const char* value = std::getenv(name);
  if (value && *value) {
    return value;
  }
  return std::nullopt;
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

RemoteStorage::Backend::Failure
failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

struct GhaConfig
{
  std::string results_url;
  std::string token;
  std::string prefix;
  bool debug = false;
  std::chrono::milliseconds connect_timeout = k_default_connect_timeout;
  std::chrono::milliseconds operation_timeout = k_default_operation_timeout;
};

bool
parse_bool(std::string_view value)
{
  return value == "1" || value == "true" || value == "yes" || value == "on";
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

GhaConfig
parse_config(const Url& url,
             const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  GhaConfig config;
  config.results_url = getenv_string("ACTIONS_RESULTS_URL").value_or("");
  if (config.results_url.empty()) {
    config.results_url = getenv_string("ACTIONS_CACHE_URL").value_or("");
  }
  config.token = getenv_string("ACTIONS_RUNTIME_TOKEN").value_or("");
  if (config.token.empty()) {
    config.token =
      getenv_string("ACTIONS_ID_TOKEN_REQUEST_TOKEN").value_or("");
  }
  config.prefix = strip_slashes(url.host() + url.path());

  for (const auto& attr : attributes) {
    if (attr.key == "url") {
      config.results_url = attr.value;
    } else if (attr.key == "url-env") {
      config.results_url = getenv_string(attr.value.c_str()).value_or("");
    } else if (attr.key == "token") {
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
      LOG("CCACHE-GHA-0008: unknown GitHub Actions cache storage attribute: {}",
          attr.key);
    }
  }

  if (config.results_url.empty()) {
    throw core::Fatal("CCACHE-GHA-0001: ACTIONS_RESULTS_URL or @url is"
                      " required for gha storage");
  }
  if (config.token.empty()) {
    throw core::Fatal("CCACHE-GHA-0002: ACTIONS_RUNTIME_TOKEN or @token is"
                      " required for gha storage");
  }
  return config;
}

Url
partial_url(const Url& url)
{
  Url partial;
  partial.scheme(url.scheme());
  partial.host(url.host(), url.ip_version());
  if (!url.port().empty()) {
    partial.port(url.port());
  }
  return partial;
}

std::string
url_path(const Url& url)
{
  auto path = url.path();
  if (path.empty()) {
    return "/";
  }
  if (path.back() != '/') {
    path += '/';
  }
  return path;
}

std::string
cache_key(const Hash::Digest& key, const std::string& prefix)
{
  const std::string digest = util::format_base16(key);
  return prefix.empty() ? digest : FMT("{}/{}", prefix, digest);
}

class GhaStorageBackend : public RemoteStorage::Backend
{
public:
  GhaStorageBackend(const Url& url,
                    const std::vector<Backend::Attribute>& attributes)
    : m_config(parse_config(url, attributes)),
      m_results_url(m_config.results_url),
      m_base_path(url_path(m_results_url)),
      m_redacted_url(storage::get_redacted_url_str_for_logging(m_results_url)),
      m_http_client(partial_url(m_results_url).str())
  {
    httplib::Headers headers;
    headers.emplace("User-Agent", FMT("ccache/{}", CCACHE_VERSION));
    headers.emplace("Authorization", FMT("Bearer {}", m_config.token));
    headers.emplace("Accept", "application/json");
    m_http_client.set_keep_alive(true);
    m_http_client.set_connection_timeout(m_config.connect_timeout);
    m_http_client.set_read_timeout(m_config.operation_timeout);
    m_http_client.set_write_timeout(m_config.operation_timeout);
    m_http_client.set_default_headers(headers);
  }

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override
  {
    const std::string entry_key = cache_key(key, m_config.prefix);
    const std::string path = FMT("{}_apis/artifactcache/cache?keys={}&version="
                                 "ccache-ng-v1",
                                 m_base_path,
                                 entry_key);
    const auto result = m_http_client.Get(path);
    if (!result || result.error() != httplib::Error::Success) {
      log_once(m_seen_errors,
               "CCACHE-GHA-0003",
               FMT("failed to query GitHub Actions cache {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE-GHA-DEBUG: GET {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status == 204 || result->status == 404) {
      return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
      log_once(m_seen_errors,
               "CCACHE-GHA-0004",
               FMT("GitHub Actions cache lookup returned status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }
    return util::Bytes(result->body.data(), result->body.size());
  }

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  std::span<const uint8_t> value,
                                  Overwrite overwrite) override
  {
    const std::string entry_key = cache_key(key, m_config.prefix);
    if (overwrite == Overwrite::no) {
      const auto existing = get(key);
      if (existing && *existing) {
        return false;
      }
      if (!existing) {
        return tl::unexpected(existing.error());
      }
    }

    const std::string path =
      FMT("{}_apis/artifactcache/cache/{}", m_base_path, entry_key);
    const auto result =
      m_http_client.Put(path,
                        reinterpret_cast<const char*>(value.data()),
                        value.size(),
                        "application/octet-stream");
    if (!result || result.error() != httplib::Error::Success) {
      log_once(m_seen_errors,
               "CCACHE-GHA-0005",
               FMT("failed to upload GitHub Actions cache entry {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE-GHA-DEBUG: PUT {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status == 403 || result->status == 429) {
      log_once(m_seen_errors,
               "CCACHE-GHA-0006",
               "GitHub Actions cache write was skipped by service policy");
      return false;
    }
    if (result->status < 200 || result->status >= 300) {
      log_once(m_seen_errors,
               "CCACHE-GHA-0007",
               FMT("GitHub Actions cache upload returned status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

  tl::expected<bool, Failure> remove(const Hash::Digest&) override
  {
    log_once(m_seen_errors,
             "CCACHE-GHA-0009",
             "GitHub Actions cache storage does not support deleting entries");
    return false;
  }

private:
  GhaConfig m_config;
  Url m_results_url;
  std::string m_base_path;
  std::string m_redacted_url;
  httplib::Client m_http_client;
  std::set<std::string> m_seen_errors;
};

} // namespace

std::unique_ptr<RemoteStorage::Backend>
GhaStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<GhaStorageBackend>(url, attributes);
}

} // namespace storage::remote
