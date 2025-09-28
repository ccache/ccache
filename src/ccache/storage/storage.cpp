// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "storage.hpp"

#include <ccache/config.hpp>
#include <ccache/core/cacheentry.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/core/statistic.hpp>
#include <ccache/storage/remote/filestorage.hpp>
#ifdef HAVE_HTTP_STORAGE_BACKEND
#  include <ccache/storage/remote/httpstorage.hpp>
#endif
#ifdef HAVE_REDIS_STORAGE_BACKEND
#  include <ccache/storage/remote/redisstorage.hpp>
#endif
#include <ccache/util/assertions.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/timer.hpp>
#include <ccache/util/tokenizer.hpp>
#include <ccache/util/xxh3_64.hpp>

#include <cxxurl/url.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace storage {

const std::unordered_map<std::string /*scheme*/,
                         std::shared_ptr<remote::RemoteStorage>>
  k_remote_storage_implementations = {
    {"file",       std::make_shared<remote::FileStorage>() },
#ifdef HAVE_HTTP_STORAGE_BACKEND
    {"http",       std::make_shared<remote::HttpStorage>() },
#endif
#ifdef HAVE_REDIS_STORAGE_BACKEND
    {"redis",      std::make_shared<remote::RedisStorage>()},
    {"redis+unix", std::make_shared<remote::RedisStorage>()},
#endif
};

std::vector<std::string>
get_features()
{
  std::vector<std::string> features;
  features.reserve(k_remote_storage_implementations.size());
  std::transform(k_remote_storage_implementations.begin(),
                 k_remote_storage_implementations.end(),
                 std::back_inserter(features),
                 [](auto& entry) { return FMT("{}-storage", entry.first); });
  return features;
}

// Representation of one shard configuration.
struct RemoteStorageShardConfig
{
  std::string name;
  double weight;
  Url url; // Cache of URL with expanded "*"
};

// Representation of one entry in the remote_storage config option.
struct RemoteStorageConfig
{
  // Raw URL with unexpanded "*".
  std::string url_str;

  // "shard" attribute.
  std::vector<RemoteStorageShardConfig> shards;

  // "read-only" attribute.
  bool read_only = false;

  // Other attributes.
  std::vector<remote::RemoteStorage::Backend::Attribute> attributes;
};

// An instantiated remote storage backend.
struct RemoteStorageBackendEntry
{
  Url url;                     // With expanded "*"
  std::string url_for_logging; // With expanded "*"
  std::unique_ptr<remote::RemoteStorage::Backend> impl;
  bool failed = false;
};

// An instantiated remote storage.
struct RemoteStorageEntry
{
  RemoteStorageConfig config;
  std::shared_ptr<remote::RemoteStorage> storage;
  std::vector<RemoteStorageBackendEntry> backends;
};

static std::string
to_string(const RemoteStorageConfig& entry)
{
  std::string result = entry.url_str;
  for (const auto& attr : entry.attributes) {
    result += FMT("|{}={}", attr.key, attr.raw_value);
  }
  return result;
}

static tl::expected<Url, std::string>
url_from_string(const std::string& url_string)
{
  // The Url class is parsing the URL object lazily. Check if the URL is valid
  // now to avoid exceptions later.
  Url url(url_string);
  try {
    std::ignore = url.str();
  } catch (const std::exception& e) {
    return tl::unexpected(FMT("Cannot parse URL {}: {}", url_string, e.what()));
  }
  if (url.scheme().empty()) {
    return tl::unexpected(FMT("URL scheme must not be empty: {}", url_string));
  }
  return url;
}

static RemoteStorageConfig
parse_storage_config(const std::string_view entry)
{
  const auto parts =
    util::split_into_views(entry, "|", util::Tokenizer::Mode::include_empty);

  if (parts.empty() || parts.front().empty()) {
    throw core::Error(
      FMT("remote storage config must provide a URL: {}", entry));
  }

  RemoteStorageConfig result;
  result.url_str = std::string(parts[0]);
  const auto& url_str = result.url_str;

  for (size_t i = 1; i < parts.size(); ++i) {
    if (parts[i].empty()) {
      continue;
    }
    const auto [key, right_hand_side] =
      util::split_once_into_views(parts[i], '=');
    const auto& raw_value = right_hand_side.value_or("true");
    const auto value =
      util::value_or_throw<core::Error>(util::percent_decode(raw_value));
    if (key == "read-only") {
      result.read_only = (value == "true");
    } else if (key == "shards") {
      const auto asterisk_count =
        std::count(url_str.begin(), url_str.end(), '*');
      if (asterisk_count == 0) {
        throw core::Error(
          FMT(R"(Missing "*" in URL when using shards: "{}")", url_str));
      } else if (asterisk_count > 1) {
        throw core::Error(
          FMT(R"(Multiple "*" in URL when using shards: "{}")", url_str));
      }
      std::string scheme;
      for (const auto& shard : util::Tokenizer(value, ",")) {
        double weight = 1.0;
        std::string_view name;
        const auto lp_pos = shard.find('(');
        if (lp_pos != std::string_view::npos) {
          if (shard.back() != ')') {
            throw core::Error(FMT("Invalid shard name: \"{}\"", shard));
          }
          weight =
            util::value_or_throw<core::Error>(util::parse_double(std::string(
              shard.substr(lp_pos + 1, shard.length() - lp_pos - 2))));
          if (weight < 0.0) {
            throw core::Error(FMT("Invalid shard weight: \"{}\"", weight));
          }
          name = shard.substr(0, lp_pos);
        } else {
          name = shard;
        }

        Url url = util::value_or_throw<core::Error>(
          url_from_string(util::replace_first(url_str, "*", name)));
        if (!scheme.empty() && url.scheme() != scheme) {
          throw core::Error(FMT("Scheme {} different from {} in {}",
                                url.scheme(),
                                scheme,
                                url_str));
        }
        result.shards.push_back({std::string(name), weight, url});
      }
    }

    result.attributes.push_back(
      {std::string(key), value, std::string(raw_value)});
  }

  // No shards => save the single URL as the sole shard.
  if (result.shards.empty()) {
    result.shards.push_back(
      {"", 0.0, util::value_or_throw<core::Error>(url_from_string(url_str))});
  }

  return result;
}

static std::vector<RemoteStorageConfig>
parse_storage_configs(const std::string_view& configs)
{
  std::vector<RemoteStorageConfig> result;
  for (const auto& config : util::Tokenizer(configs, " ")) {
    result.push_back(parse_storage_config(config));
  }
  return result;
}

static std::shared_ptr<remote::RemoteStorage>
get_storage(const std::string& scheme)
{
  const auto it = k_remote_storage_implementations.find(scheme);
  if (it != k_remote_storage_implementations.end()) {
    return it->second;
  } else {
    return {};
  }
}

std::string
get_redacted_url_str_for_logging(const Url& url)
{
  Url redacted_url(url);
  if (!url.user_info().empty()) {
    redacted_url.user_info(k_redacted_password);
  }
  return redacted_url.str();
}

Storage::Storage(const Config& config)
  : local(config),
    m_config(config)
{
}

// Define the destructor in the implementation file to avoid having to declare
// RemoteStorageEntry and its constituents in the header file.
Storage::~Storage() = default;

void
Storage::initialize()
{
  add_remote_storages();
}

void
Storage::finalize()
{
  local.finalize();
}

void
Storage::get(const Hash::Hash::Digest& key,
             const core::CacheEntryType type,
             const EntryReceiver& entry_receiver)
{
  if (!m_config.remote_only()) {
    auto value = local.get(key, type);
    if (value) {
      if (m_config.reshare()) {
        put_in_remote_storage(key, *value, Overwrite::no);
      }
      if (entry_receiver(std::move(*value))) {
        return;
      }
    }
  }

  get_from_remote_storage(key, type, [&](util::Bytes&& data) {
    if (!m_config.remote_only()) {
      local.put(key, type, data, Overwrite::no);
    }
    return entry_receiver(std::move(data));
  });
}

void
Storage::put(const Hash::Digest& key,
             const core::CacheEntryType type,
             nonstd::span<const uint8_t> value)
{
  if (!m_config.remote_only()) {
    local.put(key, type, value, Overwrite::yes);
  }
  put_in_remote_storage(key, value, Overwrite::yes);
}

void
Storage::remove(const Hash::Digest& key, const core::CacheEntryType type)
{
  if (!m_config.remote_only()) {
    local.remove(key, type);
  }
  remove_from_remote_storage(key);
}

bool
Storage::has_remote_storage() const
{
  return !m_remote_storages.empty();
}

std::string
Storage::get_remote_storage_config_for_logging() const
{
  auto configs = parse_storage_configs(m_config.remote_storage());
  for (auto& config : configs) {
    const auto url = url_from_string(config.url_str);
    if (url) {
      const auto storage = get_storage(url->scheme());
      if (storage) {
        config.url_str = get_redacted_url_str_for_logging(*url);
        storage->redact_secrets(config.attributes);
      }
    } // else: unexpanded URL is not a proper URL, not much we can do
  }
  return util::join(configs, " ");
}

void
Storage::add_remote_storages()
{
  const auto configs = parse_storage_configs(m_config.remote_storage());
  for (const auto& config : configs) {
    ASSERT(!config.shards.empty());
    const std::string scheme = config.shards.front().url.scheme();
    const auto storage = get_storage(scheme);
    if (!storage) {
      throw core::Error(FMT("unknown remote storage scheme: {}", scheme));
    }
    m_remote_storages.push_back(std::make_unique<RemoteStorageEntry>(
      RemoteStorageEntry{config, storage, {}}));
  }
}

void
Storage::mark_backend_as_failed(
  RemoteStorageBackendEntry& backend_entry,
  const remote::RemoteStorage::Backend::Failure failure)
{
  // The backend is expected to log details about the error.
  backend_entry.failed = true;
  local.increment_statistic(
    failure == remote::RemoteStorage::Backend::Failure::timeout
      ? core::Statistic::remote_storage_timeout
      : core::Statistic::remote_storage_error);
}

static double
to_half_open_unit_interval(uint64_t value)
{
  constexpr uint8_t double_significand_bits = 53;
  constexpr uint64_t denominator = 1ULL << double_significand_bits;
  constexpr uint64_t mask = denominator - 1;
  return static_cast<double>(value & mask) / denominator;
}

static Url
get_shard_url(const Hash::Digest& key,
              const std::vector<RemoteStorageShardConfig>& shards)
{
  ASSERT(!shards.empty());

  if (shards.size() == 1) {
    return shards.front().url;
  }

  // This is the "weighted rendezvous hashing" algorithm.
  double highest_score = -1.0;
  Url best_shard_url;
  for (const auto& shard_config : shards) {
    util::XXH3_64 hash;
    hash.update(key.data(), key.size());
    hash.update(shard_config.name.data(), shard_config.name.length());
    const double score = to_half_open_unit_interval(hash.digest());
    ASSERT(score >= 0.0 && score < 1.0);
    const double weighted_score =
      score == 0.0 ? 0.0 : shard_config.weight / -std::log(score);
    if (weighted_score > highest_score) {
      best_shard_url = shard_config.url;
      highest_score = weighted_score;
    }
  }

  return best_shard_url;
}

RemoteStorageBackendEntry*
Storage::get_backend(RemoteStorageEntry& entry,
                     const Hash::Digest& key,
                     const std::string_view operation_description,
                     const bool for_writing)
{
  if (for_writing && entry.config.read_only) {
    LOG("Not {} {} storage since it is read-only",
        operation_description,
        entry.config.shards.front().url.scheme());
    return nullptr;
  }

  const auto shard_url = get_shard_url(key, entry.config.shards);
  const auto url_str_for_logging =
    get_redacted_url_str_for_logging(shard_url.str());
  auto backend =
    std::find_if(entry.backends.begin(),
                 entry.backends.end(),
                 [&](const auto& x) { return x.url.str() == shard_url.str(); });

  if (backend == entry.backends.end()) {
    entry.backends.push_back({shard_url, url_str_for_logging, {}, false});
    try {
      entry.backends.back().impl =
        entry.storage->create_backend(shard_url, entry.config.attributes);
    } catch (const remote::RemoteStorage::Backend::Failed& e) {
      LOG("Failed to construct backend for {}{}",
          url_str_for_logging,
          std::string_view(e.what()).empty() ? "" : FMT(": {}", e.what()));
      mark_backend_as_failed(entry.backends.back(), e.failure());
      return nullptr;
    }
    return &entry.backends.back();
  } else if (backend->failed) {
    LOG("Not {} {} since it failed earlier",
        operation_description,
        url_str_for_logging);
    return nullptr;
  } else {
    return &*backend;
  }
}

void
Storage::get_from_remote_storage(const Hash::Digest& key,
                                 const core::CacheEntryType type,
                                 const EntryReceiver& entry_receiver)
{
  for (const auto& entry : m_remote_storages) {
    auto backend = get_backend(*entry, key, "getting from", false);
    if (!backend) {
      continue;
    }

    Timer timer;
    auto result = backend->impl->get(key);
    const auto ms = timer.measure_ms();
    if (!result) {
      mark_backend_as_failed(*backend, result.error());
      continue;
    }

    auto& value = *result;
    if (value) {
      LOG("Retrieved {} from {} ({:.2f} ms)",
          util::format_digest(key),
          backend->url_for_logging,
          ms);
      local.increment_statistic(core::Statistic::remote_storage_read_hit);
      if (type == core::CacheEntryType::result) {
        local.increment_statistic(core::Statistic::remote_storage_hit);
      }
      if (entry_receiver(std::move(*value))) {
        return;
      }
    } else {
      LOG("No {} in {} ({:.2f} ms)",
          util::format_digest(key),
          backend->url_for_logging,
          ms);
      local.increment_statistic(core::Statistic::remote_storage_read_miss);
    }
  }
}

void
Storage::put_in_remote_storage(const Hash::Digest& key,
                               nonstd::span<const uint8_t> value,
                               Overwrite overwrite)
{
  if (!core::CacheEntry::Header(value).self_contained) {
    LOG("Not putting {} in remote storage since it's not self-contained",
        util::format_digest(key));
    return;
  }

  for (const auto& entry : m_remote_storages) {
    auto backend = get_backend(*entry, key, "putting in", true);
    if (!backend) {
      continue;
    }

    Timer timer;
    const auto result = backend->impl->put(key, value, overwrite);
    const auto ms = timer.measure_ms();
    if (!result) {
      // The backend is expected to log details about the error.
      mark_backend_as_failed(*backend, result.error());
      continue;
    }

    const bool stored = *result;
    LOG("{} {} in {} ({:.2f} ms)",
        stored ? "Stored" : "Did not have to store",
        util::format_digest(key),
        backend->url_for_logging,
        ms);
    local.increment_statistic(core::Statistic::remote_storage_write);
  }
}

void
Storage::remove_from_remote_storage(const Hash::Digest& key)
{
  for (const auto& entry : m_remote_storages) {
    auto backend = get_backend(*entry, key, "removing from", true);
    if (!backend) {
      continue;
    }

    Timer timer;
    const auto result = backend->impl->remove(key);
    const auto ms = timer.measure_ms();
    if (!result) {
      mark_backend_as_failed(*backend, result.error());
      continue;
    }

    const bool removed = *result;
    if (removed) {
      LOG("Removed {} from {} ({:.2f} ms)",
          util::format_digest(key),
          backend->url_for_logging,
          ms);
    } else {
      LOG("No {} to remove from {} ({:.2f} ms)",
          util::format_digest(key),
          backend->url_for_logging,
          ms);
    }

    local.increment_statistic(core::Statistic::remote_storage_write);
  }
}

} // namespace storage
