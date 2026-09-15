// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "ocistorage.hpp"
#include "httptransport.hpp"

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
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace storage::remote {

namespace {

using detail::http_failure_from_httplib_error;

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
read_token_file(const std::string& path)
{
#ifndef _WIN32
  struct stat file_status = {};
  if (stat(path.c_str(), &file_status) != 0 || !S_ISREG(file_status.st_mode)) {
    throw core::Fatal("CCACHE_NG-ERROR-OCI-0032: unable to read OCI token file");
  }
  if ((file_status.st_mode & (S_IRWXG | S_IRWXO)) != 0
      || (file_status.st_uid != geteuid() && file_status.st_uid != 0)) {
    throw core::Fatal("CCACHE_NG-ERROR-OCI-0033: OCI token file is not private");
  }
#endif

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw core::Fatal("CCACHE_NG-ERROR-OCI-0032: unable to read OCI token file");
  }
  std::string token(std::istreambuf_iterator<char>(input), {});
  if (input.bad()) {
    throw core::Fatal("CCACHE_NG-ERROR-OCI-0032: unable to read OCI token file");
  }
  while (!token.empty() && (token.back() == '\n' || token.back() == '\r')) {
    token.pop_back();
  }
  if (token.empty()) {
    throw core::Fatal("CCACHE_NG-ERROR-OCI-0034: OCI token file is empty");
  }
  return token;
}

uint32_t
rotate_right(const uint32_t value, const uint32_t bits)
{
  return (value >> bits) | (value << (32 - bits));
}

std::string
sha256_hex(std::span<const uint8_t> value)
{
  static constexpr std::array<uint32_t, 64> k_round_constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<uint32_t, 8> state = {0x6a09e667,
                                   0xbb67ae85,
                                   0x3c6ef372,
                                   0xa54ff53a,
                                   0x510e527f,
                                   0x9b05688c,
                                   0x1f83d9ab,
                                   0x5be0cd19};

  std::vector<uint8_t> message(value.begin(), value.end());
  const uint64_t bit_length = static_cast<uint64_t>(message.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56) {
    message.push_back(0);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<uint8_t>(bit_length >> shift));
  }

  for (size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<uint32_t, 64> words = {};
    for (size_t i = 0; i < 16; ++i) {
      const size_t byte = offset + i * 4;
      words[i] = (static_cast<uint32_t>(message[byte]) << 24)
                 | (static_cast<uint32_t>(message[byte + 1]) << 16)
                 | (static_cast<uint32_t>(message[byte + 2]) << 8)
                 | message[byte + 3];
    }
    for (size_t i = 16; i < words.size(); ++i) {
      const uint32_t s0 = rotate_right(words[i - 15], 7)
                          ^ rotate_right(words[i - 15], 18)
                          ^ (words[i - 15] >> 3);
      const uint32_t s1 = rotate_right(words[i - 2], 17)
                          ^ rotate_right(words[i - 2], 19)
                          ^ (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (size_t i = 0; i < words.size(); ++i) {
      const uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11)
                          ^ rotate_right(e, 25);
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + s1 + choice + k_round_constants[i] + words[i];
      const uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13)
                          ^ rotate_right(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      result += digits[(word >> shift) & 0x0f];
    }
  }
  return result;
}

void
log_diagnostic(const std::string& code, const std::string& message)
{
  LOG("{}: {}", code, message);
}

std::optional<std::string>
extract_oci_layer_digest(std::string_view manifest)
{
  const size_t layers_pos = manifest.find("\"layers\"");
  if (layers_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t digest_key_pos = manifest.find("\"digest\"", layers_pos);
  if (digest_key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t value_start = manifest.find('"', digest_key_pos + 8);
  if (value_start == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t value_end = manifest.find('"', value_start + 1);
  if (value_end == std::string_view::npos) {
    return std::nullopt;
  }
  const auto digest = manifest.substr(value_start + 1, value_end - value_start - 1);
  return digest.starts_with("sha256:") ? std::optional<std::string>(digest)
                                       : std::nullopt;
}

class OciStorageBackend : public RemoteStorage::Backend
{
public:
  OciStorageBackend(const Url& url,
    const std::vector<Backend::Attribute>& attributes)
    : m_config(detail::parse_oci_storage_config(url, attributes)),
      m_redacted_url(storage::get_redacted_url_str_for_logging(url)),
      m_http_client(FMT("{}://{}", m_config.insecure ? "http" : "https", m_config.registry))
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
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-OCI-9001: initialized {} transport={}",
          m_redacted_url,
          m_config.insecure ? "http" : "https");
    }
  }

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override
  {
    const std::string entry_key =
      detail::make_oci_storage_key(key, m_config.prefix);
    const std::string tag = detail::make_oci_manifest_tag(entry_key);
    const std::string manifest_path =
      detail::make_oci_manifest_path(m_config.repository, tag);
    httplib::Headers headers;
    headers.emplace("Accept", "application/vnd.oci.image.manifest.v1+json");
    const auto manifest = m_http_client.Get(manifest_path, headers);
    if (!manifest || manifest.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0001",
               FMT("failed to get OCI manifest from {}: {}",
                   m_redacted_url,
                   to_string(manifest.error())));
      return tl::unexpected(http_failure_from_httplib_error(manifest.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-OCI-9002: GET manifest {} key={} status={}",
          m_redacted_url,
          entry_key,
          manifest->status);
    }
    if (manifest->status == 404) {
      return std::nullopt;
    }
    if (manifest->status < 200 || manifest->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0002",
               FMT("OCI manifest lookup returned status {}", manifest->status));
      return tl::unexpected(Failure::error);
    }

    const auto blob_digest = extract_oci_layer_digest(manifest->body);
    if (!blob_digest) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0010",
               "OCI manifest did not contain a cache layer digest");
      return tl::unexpected(Failure::error);
    }
    const auto blob = m_http_client.Get(
      detail::make_oci_blob_path(m_config.repository, *blob_digest));
    if (!blob || blob.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0011",
               FMT("failed to get OCI cache blob from {}: {}",
                   m_redacted_url,
                   to_string(blob.error())));
      return tl::unexpected(http_failure_from_httplib_error(blob.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-OCI-9003: GET blob {} key={} status={}",
          m_redacted_url,
          entry_key,
          blob->status);
    }
    if (blob->status < 200 || blob->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0012",
               FMT("OCI cache blob download returned status {}", blob->status));
      return tl::unexpected(Failure::error);
    }
    return util::Bytes(blob->body.data(), blob->body.size());
  }

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  std::span<const uint8_t> value,
                                  Overwrite overwrite) override
  {
    const std::string entry_key =
      detail::make_oci_storage_key(key, m_config.prefix);
    const std::string tag = detail::make_oci_manifest_tag(entry_key);
    const std::string manifest_path =
      detail::make_oci_manifest_path(m_config.repository, tag);
    if (overwrite == Overwrite::no) {
      const auto head = m_http_client.Head(manifest_path);
      if (head && head->status >= 200 && head->status < 300) {
        return false;
      }
    }

    constexpr std::array<uint8_t, 2> empty_config = {'{', '}'};
    const std::string config_digest = detail::make_oci_blob_digest(empty_config);
    if (const auto config = upload_blob(config_digest, empty_config); !config) {
      return tl::unexpected(config.error());
    }
    const std::string blob_digest = detail::make_oci_blob_digest(value);
    if (const auto blob = upload_blob(blob_digest, value); !blob) {
      return tl::unexpected(blob.error());
    }

    const std::string manifest = FMT(
      "{{\"schemaVersion\":2,\"mediaType\":"
      "\"application/vnd.oci.image.manifest.v1+json\",\"artifactType\":"
      "\"application/vnd.ccache.entry\",\"config\":{{\"mediaType\":"
      "\"application/vnd.oci.empty.v1+json\",\"digest\":\"{}\","
      "\"size\":2}},\"layers\":[{{\"mediaType\":"
      "\"application/vnd.ccache.entry\",\"digest\":\"{}\",\"size\":{}}}]}}",
      config_digest,
      blob_digest,
      value.size());
    const auto result = m_http_client.Put(manifest_path,
                                          manifest,
                                          "application/vnd.oci.image.manifest.v1+json");
    if (!result || result.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0027",
               FMT("failed to publish OCI cache manifest to {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(http_failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-OCI-9004: PUT manifest {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status < 200 || result->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0028",
               FMT("OCI registry rejected manifest upload with status {}",
                   result->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override
  {
    const std::string entry_key =
      detail::make_oci_storage_key(key, m_config.prefix);
    const std::string tag = detail::make_oci_manifest_tag(entry_key);
    const std::string manifest_path =
      detail::make_oci_manifest_path(m_config.repository, tag);
    const auto manifest = m_http_client.Head(manifest_path);
    if (!manifest || manifest.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0005",
               FMT("failed to get OCI manifest for deletion from {}: {}",
                   m_redacted_url,
                   to_string(manifest.error())));
      return tl::unexpected(http_failure_from_httplib_error(manifest.error()));
    }
    if (manifest->status == 404) {
      return false;
    }
    if (manifest->status < 200 || manifest->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0006",
               FMT("OCI manifest lookup for deletion returned status {}",
                   manifest->status));
      return tl::unexpected(Failure::error);
    }
    const std::string manifest_digest =
      manifest->get_header_value("Docker-Content-Digest");
    if (manifest_digest.empty()) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0029",
               "OCI manifest lookup for deletion had no content digest");
      return tl::unexpected(Failure::error);
    }
    const auto result = m_http_client.Delete(
      detail::make_oci_manifest_path(m_config.repository, manifest_digest));
    if (!result || result.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0030",
               FMT("failed to delete OCI manifest from {}: {}",
                   m_redacted_url,
                   to_string(result.error())));
      return tl::unexpected(http_failure_from_httplib_error(result.error()));
    }
    if (m_config.debug) {
      LOG("CCACHE_NG-DEBUG-OCI-9005: DELETE manifest {} key={} status={}",
          m_redacted_url,
          entry_key,
          result->status);
    }
    if (result->status < 200 || result->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0031",
               FMT("OCI manifest deletion returned status {}", result->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

private:
  tl::expected<bool, Failure> upload_blob(const std::string& digest,
                                          std::span<const uint8_t> value)
  {
    const std::string blob_path =
      detail::make_oci_blob_path(m_config.repository, digest);
    const auto existing = m_http_client.Head(blob_path);
    if (!existing || existing.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0020",
               FMT("failed to check OCI blob in {}: {}",
                   m_redacted_url,
                   to_string(existing.error())));
      return tl::unexpected(http_failure_from_httplib_error(existing.error()));
    }
    if (existing->status >= 200 && existing->status < 300) {
      return false;
    }
    if (existing->status != 404) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0021",
               FMT("OCI blob check returned status {}", existing->status));
      return tl::unexpected(Failure::error);
    }

    const auto start = m_http_client.Post(
      FMT("/v2/{}/blobs/uploads/", m_config.repository));
    if (!start || start.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0022",
               FMT("failed to start OCI blob upload to {}: {}",
                   m_redacted_url,
                   to_string(start.error())));
      return tl::unexpected(http_failure_from_httplib_error(start.error()));
    }
    if (start->status < 200 || start->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0023",
               FMT("OCI blob upload start returned status {}", start->status));
      return tl::unexpected(Failure::error);
    }
    const std::string location = start->get_header_value("Location");
    if (location.empty()) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0024",
               "OCI blob upload start response had no location");
      return tl::unexpected(Failure::error);
    }
    const size_t scheme = location.find("://");
    const size_t path_start = scheme == std::string::npos
                                ? 0
                                : location.find('/', scheme + 3);
    std::string complete_path = path_start == std::string::npos
                                  ? "/"
                                  : location.substr(path_start);
    complete_path += complete_path.find('?') == std::string::npos ? "?digest=" : "&digest=";
    complete_path += digest;
    const auto complete = m_http_client.Put(
      complete_path,
      reinterpret_cast<const char*>(value.data()),
      value.size(),
      "application/octet-stream");
    if (!complete || complete.error() != httplib::Error::Success) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0025",
               FMT("failed to complete OCI blob upload to {}: {}",
                   m_redacted_url,
                   to_string(complete.error())));
      return tl::unexpected(http_failure_from_httplib_error(complete.error()));
    }
    if (complete->status < 200 || complete->status >= 300) {
      log_diagnostic(
               "CCACHE_NG-ERROR-OCI-0026",
               FMT("OCI blob upload completion returned status {}",
                   complete->status));
      return tl::unexpected(Failure::error);
    }
    return true;
  }

  detail::OciStorageConfig m_config;
  std::string m_redacted_url;
  httplib::Client m_http_client;
};

} // namespace

namespace detail {

OciStorageConfig
parse_oci_storage_config(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes)
{
  if (url.host().empty()) {
    throw core::Fatal(FMT(
      "CCACHE_NG-ERROR-OCI-0007: registry host is required in OCI storage URL \"{}\"",
      storage::get_redacted_url_str_for_logging(url)));
  }

  OciStorageConfig config;
  config.debug = parse_bool(getenv_string("ACTIONS_STEP_DEBUG").value_or(""));
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
      "CCACHE_NG-ERROR-OCI-0008: repository path is required in OCI storage URL \"{}\"",
      storage::get_redacted_url_str_for_logging(url)));
  }

  for (const auto& attr : attributes) {
    if (attr.key == "token") {
      config.token = attr.value;
    } else if (attr.key == "token-env") {
      config.token = getenv_string(attr.value.c_str()).value_or("");
    } else if (attr.key == "token-file") {
      config.token = read_token_file(attr.value);
    } else if (attr.key == "token-file-env") {
      const auto token_path = getenv_string(attr.value.c_str());
      if (!token_path) {
        throw core::Fatal("CCACHE_NG-ERROR-OCI-0032: unable to read OCI token file");
      }
      config.token = read_token_file(*token_path);
    } else if (attr.key == "prefix") {
      config.prefix = strip_slashes(attr.value);
    } else if (attr.key == "insecure") {
      config.insecure = parse_bool(attr.value);
    } else if (attr.key == "debug") {
      config.debug = parse_bool(attr.value);
    } else if (attr.key == "connect-timeout") {
      config.connect_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      config.operation_timeout =
        RemoteStorage::Backend::parse_timeout_attribute(attr.value);
    } else {
      LOG("CCACHE_NG-WARN-OCI-0009: unknown OCI storage attribute: {}", attr.key);
    }
  }

  return config;
}

std::string
make_oci_storage_key(const Hash::Digest& key, const std::string& prefix)
{
  const std::string digest = util::format_base16(key);
  return prefix.empty() ? digest : FMT("{}/{}", prefix, digest);
}

std::string
make_oci_manifest_tag(const std::string& key)
{
  const auto separator = key.rfind('/');
  const std::string_view key_view(key);
  const std::string_view prefix =
    separator == std::string::npos ? std::string_view() : key_view.substr(0, separator);
  const std::string_view digest =
    separator == std::string::npos ? key_view : key_view.substr(separator + 1);
  if (prefix.empty()) {
    return FMT("ccache-{}", digest);
  }
  const auto prefix_bytes = std::span<const uint8_t>(
    reinterpret_cast<const uint8_t*>(prefix.data()), prefix.size());
  return FMT("ccache-{}-{}", sha256_hex(prefix_bytes), digest);
}

std::string
make_oci_blob_digest(const std::span<const uint8_t> value)
{
  return FMT("sha256:{}", sha256_hex(value));
}

std::string
make_oci_blob_path(const std::string& repository, const std::string& digest)
{
  return FMT("/v2/{}/blobs/{}", repository, digest);
}

std::string
make_oci_manifest_path(const std::string& repository,
                       const std::string& tag_or_digest)
{
  return FMT("/v2/{}/manifests/{}", repository, tag_or_digest);
}

} // namespace detail

std::unique_ptr<RemoteStorage::Backend>
OciStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<OciStorageBackend>(url, attributes);
}

} // namespace storage::remote
