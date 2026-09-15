// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "ghastorage.hpp"
#include "remoteutils.hpp"

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

#include <optional>
#include <string>
#include <string_view>

namespace storage::remote {

namespace {

using detail::failure_from_httplib_error;
using detail::getenv_string;
using detail::log_diagnostic;
using detail::parse_bool;
using detail::partial_url;
using detail::strip_slashes;
using detail::url_path;

std::string
json_quote(std::string_view value)
{
  std::string result = "\"";
  for (const char c : value) {
    switch (c) {
    case '\"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
      break;
    }
  }
  result += '"';
  return result;
}

std::optional<std::string>
extract_json_string(std::string_view response_body, std::string_view key)
{
  const std::string quoted_key = FMT("\"{}\"", key);
  const size_t key_pos = response_body.find(quoted_key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t pos = response_body.find(':', key_pos + quoted_key.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < response_body.size()
         && (response_body[pos] == ' ' || response_body[pos] == '\t'
             || response_body[pos] == '\r' || response_body[pos] == '\n')) {
    ++pos;
  }
  if (pos == response_body.size() || response_body[pos] != '"') {
    return std::nullopt;
  }
  ++pos;

  std::string value;
  bool escaped = false;
  for (; pos < response_body.size(); ++pos) {
    const char c = response_body[pos];
    if (escaped) {
      switch (c) {
      case '"':
      case '\\':
      case '/':
        value.push_back(c);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return std::nullopt;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return value;
    } else {
      value.push_back(c);
    }
  }

  return std::nullopt;
}

bool
extract_json_true(std::string_view response_body, std::string_view key)
{
  const std::string quoted_key = FMT("\"{}\"", key);
  const size_t key_pos = response_body.find(quoted_key);
  if (key_pos == std::string_view::npos) {
    return false;
  }
  const size_t value_pos = response_body.find(':', key_pos + quoted_key.size());
  if (value_pos == std::string_view::npos) {
    return false;
  }
  const auto value = response_body.substr(value_pos + 1);
  const size_t first = value.find_first_not_of(" \t\r\n");
  return first != std::string_view::npos && value.substr(first, 4) == "true";
}

std::optional<std::string>
extract_json_number(std::string_view response_body, std::string_view key)
{
  const std::string quoted_key = FMT("\"{}\"", key);
  const size_t key_pos = response_body.find(quoted_key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t value_pos = response_body.find(':', key_pos + quoted_key.size());
  if (value_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto value = response_body.substr(value_pos + 1);
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos || value[first] < '0'
      || value[first] > '9') {
    return std::nullopt;
  }
  const size_t last = value.find_first_not_of("0123456789", first);
  return std::string(value.substr(first, last - first));
}

class GhaStorageBackend : public RemoteStorage::Backend
{
public:
  GhaStorageBackend(const Url& url,
    const std::vector<Backend::Attribute>& attributes)
    : m_config(detail::parse_gha_storage_config(url, attributes)),
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
    const std::string entry_key =
      detail::make_gha_storage_key(key, m_config.prefix);
    const std::string cache_version = detail::gha_cache_version(
      m_config.service_version);
    const bool v2 = m_config.service_version == detail::GhaServiceVersion::v2;
    const std::string path = v2
                               ? FMT("{}twirp/github.actions.results.api.v1."
                                     "CacheService/GetCacheEntryDownloadURL",
                                     m_base_path)
                               : FMT("{}_apis/artifactcache/cache?keys={}&version={}",
                                     m_base_path,
                                     httplib::detail::encode_query_param(entry_key),
                                     cache_version);
    const auto result = v2
                          ? m_http_client.Post(
                              path,
                              FMT("{{\"key\":{},\"version\":{}}}",
                                  json_quote(entry_key),
                                  json_quote(cache_version)),
                              "application/json")
                          : m_http_client.Get(path);
    if (!result || result.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0003",
               FMT("failed to query GitHub Actions cache {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-GHA-9001: GET {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (!v2 && (result->status == 204 || result->status == 404)) {
      return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0004",
               FMT("GitHub Actions cache lookup returned status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }

    const auto archive_location = v2
                                    ? extract_json_string(result->body,
                                                          "signed_download_url")
                                    : detail::extract_gha_archive_location(
                                        result->body);
    if (v2 && !extract_json_true(result->body, "ok")) {
      return std::nullopt;
    }
    if (!archive_location) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0016",
               "GitHub Actions cache lookup response had no download location");
      return tl::unexpected(Failure::error);
    }

    const Url archive_url(*archive_location);
    httplib::Client archive_client(partial_url(archive_url).str());
    archive_client.set_keep_alive(true);
    archive_client.set_connection_timeout(m_config.connect_timeout);
    archive_client.set_read_timeout(m_config.operation_timeout);
    archive_client.set_write_timeout(m_config.operation_timeout);

    const auto archive =
      archive_client.Get(detail::make_gha_archive_path(*archive_location));
    if (!archive || archive.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0017",
               FMT("failed to download GitHub Actions cache entry {}: {}",
                   storage::get_redacted_url_str_for_logging(archive_url),
                   to_string(archive.error())));
      return tl::unexpected(failure_from_httplib_error(archive.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-GHA-9002: DOWNLOAD {} key={} status={}",
          storage::get_redacted_url_str_for_logging(archive_url),
          entry_key,
          archive->status);
    }
    if (archive->status < 200 || archive->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0018",
               FMT("GitHub Actions cache download returned status {}",
                   archive->status));
      return tl::unexpected(Failure::error);
    }
    return util::Bytes(archive->body.data(), archive->body.size());
  }

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  std::span<const uint8_t> value,
                                  Overwrite overwrite) override
  {
    if (m_writes_disabled) {
      return false;
    }

    const std::string entry_key =
      detail::make_gha_storage_key(key, m_config.prefix);
    if (overwrite == Overwrite::no
        && m_config.service_version == detail::GhaServiceVersion::v1) {
      const auto existing = get(key);
      if (existing && *existing) {
        return false;
      }
      if (!existing) {
        return tl::unexpected(existing.error());
      }
    }

    const std::string cache_version = detail::gha_cache_version(
      m_config.service_version);
    const bool v2 = m_config.service_version == detail::GhaServiceVersion::v2;
    const std::string reserve_path = v2
                                      ? FMT("{}twirp/github.actions.results.api.v1."
                                            "CacheService/CreateCacheEntry",
                                            m_base_path)
                                      : FMT("{}_apis/artifactcache/caches",
                                            m_base_path);
    const std::string reserve_body = FMT("{{\"key\":{},\"version\":{}}}",
                                         json_quote(entry_key),
                                         json_quote(cache_version));
    const auto reserve =
      m_http_client.Post(reserve_path, reserve_body, "application/json");
    if (!reserve || reserve.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0005",
               FMT("failed to reserve GitHub Actions cache entry {}: {}",
                   m_redacted_url,
                   to_string(reserve.error())));
      return tl::unexpected(failure_from_httplib_error(reserve.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-GHA-9003: RESERVE {} key={} status={}",
          m_redacted_url,
          entry_key,
          reserve->status);
    }
    if (reserve->status == 403 || reserve->status == 429) {
      m_writes_disabled = true;
      log_diagnostic(
               "CCACHE_NG-WARN-GHA-0006",
               FMT("GitHub Actions cache write was disabled for this run after status {}",
                   reserve->status));
      return false;
    }
    if (reserve->status < 200 || reserve->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0007",
               FMT("GitHub Actions cache reserve returned status {}",
                   reserve->status));
      return tl::unexpected(Failure::error);
    }

    const auto upload_location =
      v2 ? extract_json_string(reserve->body, "signed_upload_url")
         : extract_json_number(reserve->body, "cacheId");
    if ((v2 && !extract_json_true(reserve->body, "ok")) || !upload_location) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0010",
               "GitHub Actions cache reserve response was incomplete");
      return v2 ? tl::expected<bool, Failure>(false)
                : tl::unexpected(Failure::error);
    }

    const std::string upload_path =
      v2 ? detail::make_gha_archive_path(*upload_location)
         : FMT("{}_apis/artifactcache/caches/{}", m_base_path, *upload_location);
    Url upload_url = v2 ? Url(*upload_location) : m_results_url;
    httplib::Client upload_client(partial_url(upload_url).str());
    upload_client.set_keep_alive(true);
    upload_client.set_connection_timeout(m_config.connect_timeout);
    upload_client.set_read_timeout(m_config.operation_timeout);
    upload_client.set_write_timeout(m_config.operation_timeout);
    httplib::Headers upload_headers;
    if (!v2) {
      upload_headers.emplace("Authorization", FMT("Bearer {}", m_config.token));
      upload_headers.emplace(
        "Content-Range", FMT("bytes 0-{}/*", value.size() - 1));
    }
    const auto upload = v2
                          ? upload_client.Put(
                              upload_path,
                              reinterpret_cast<const char*>(value.data()),
                              value.size(),
                              "application/octet-stream")
                          : upload_client.Patch(
                              upload_path,
                              upload_headers,
                              reinterpret_cast<const char*>(value.data()),
                              value.size(),
                              "application/octet-stream");
    if (!upload || upload.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0011",
               FMT("failed to upload GitHub Actions cache entry {}: {}",
                   storage::get_redacted_url_str_for_logging(upload_url),
                   to_string(upload.error())));
      return tl::unexpected(failure_from_httplib_error(upload.error()));
    }
    if (upload->status < 200 || upload->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0012",
               FMT("GitHub Actions cache upload returned status {}", upload->status));
      return tl::unexpected(Failure::error);
    }

    const std::string finalize_path = v2
                                        ? FMT("{}twirp/github.actions.results.api.v1."
                                              "CacheService/FinalizeCacheEntryUpload",
                                              m_base_path)
                                        : upload_path;
    const std::string finalize_body = v2
                                        ? FMT("{{\"key\":{},\"size_bytes\":{},"
                                              "\"version\":{}}}",
                                              json_quote(entry_key),
                                              json_quote(std::to_string(value.size())),
                                              json_quote(cache_version))
                                        : FMT("{{\"size\":{}}}", value.size());
    const auto finalize =
      m_http_client.Post(finalize_path, finalize_body, "application/json");
    if (!finalize || finalize.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0013",
               FMT("failed to finalize GitHub Actions cache entry {}: {}",
                   m_redacted_url,
                   to_string(finalize.error())));
      return tl::unexpected(failure_from_httplib_error(finalize.error()));
    }
    if (finalize->status == 403 || finalize->status == 429) {
      m_writes_disabled = true;
      log_diagnostic(
               "CCACHE_NG-WARN-GHA-0019",
               FMT("GitHub Actions cache write was disabled for this run after status {}",
                   finalize->status));
      return false;
    }
    if (finalize->status < 200 || finalize->status >= 300
        || (v2 && !extract_json_true(finalize->body, "ok"))) {
      log_diagnostic(
               "CCACHE_NG-ERROR-GHA-0014",
               FMT("GitHub Actions cache finalize returned status {}",
                   finalize->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

  tl::expected<bool, Failure> remove(const Hash::Digest&) override
  {
    log_diagnostic(
             "CCACHE_NG-ERROR-GHA-0009",
             "GitHub Actions cache storage does not support deleting entries");
    return false;
  }

private:
  detail::GhaStorageConfig m_config;
  Url m_results_url;
  std::string m_base_path;
  std::string m_redacted_url;
  httplib::Client m_http_client;
  bool m_writes_disabled = false;
};

} // namespace

namespace detail {

GhaStorageConfig
parse_gha_storage_config(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  GhaStorageConfig config;
  config.debug = parse_bool(getenv_string("ACTIONS_STEP_DEBUG").value_or(""));
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
  config.service_version = getenv_string("ACTIONS_CACHE_SERVICE_V2").has_value()
                           ? GhaServiceVersion::v2
                           : GhaServiceVersion::v1;

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
    } else if (attr.key == "service-version") {
      if (attr.value == "v1") {
        config.service_version = GhaServiceVersion::v1;
      } else if (attr.value == "v2") {
        config.service_version = GhaServiceVersion::v2;
      } else {
        throw core::Fatal(
          "CCACHE_NG-ERROR-GHA-0015: service-version must be v1 or v2 for gha storage");
      }
    } else if (attr.key == "debug") {
      config.debug = parse_bool(attr.value);
    } else if (attr.key == "connect-timeout") {
      config.connect_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      config.operation_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else {
      LOG("CCACHE_NG-WARN-GHA-0008: unknown GitHub Actions cache storage attribute: {}",
          attr.key);
    }
  }

  if (config.results_url.empty()) {
    throw RemoteStorage::Backend::Failed(
      "CCACHE_NG-ERROR-GHA-0001: ACTIONS_RESULTS_URL or @url is required for gha storage");
  }
  if (config.token.empty()) {
    throw RemoteStorage::Backend::Failed(
      "CCACHE_NG-ERROR-GHA-0002: ACTIONS_RUNTIME_TOKEN or @token is required for gha storage");
  }
  return config;
}

std::string
make_gha_storage_key(const Hash::Digest& key, const std::string& prefix)
{
  const std::string digest = util::format_base16(key);
  return prefix.empty() ? digest : FMT("{}/{}", prefix, digest);
}

std::optional<std::string>
extract_gha_archive_location(std::string_view response_body)
{
  return extract_json_string(response_body, "archiveLocation");
}

std::string
make_gha_archive_path(std::string_view archive_location)
{
  size_t authority_pos = archive_location.find("://");
  if (authority_pos == std::string_view::npos) {
    return "/";
  }
  authority_pos += 3;

  size_t path_pos = archive_location.find('/', authority_pos);
  if (path_pos == std::string_view::npos) {
    path_pos = archive_location.find('?', authority_pos);
  }
  if (path_pos == std::string_view::npos) {
    return "/";
  }

  size_t fragment_pos = archive_location.find('#', path_pos);
  if (fragment_pos == std::string_view::npos) {
    fragment_pos = archive_location.size();
  }

  const auto path = archive_location.substr(path_pos, fragment_pos - path_pos);
  return path.front() == '?' ? FMT("/{}", path) : std::string(path);
}

std::string
gha_cache_version(const GhaServiceVersion service_version)
{
  return service_version == GhaServiceVersion::v2
           ? "0923af7a82378b9fbe2fcfc3bc65175ea5a8508a02410190399fa7b6e9a51891"
           : "ccache-ng-v1";
}

} // namespace detail

std::unique_ptr<RemoteStorage::Backend>
GhaStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<GhaStorageBackend>(url, attributes);
}

} // namespace storage::remote
