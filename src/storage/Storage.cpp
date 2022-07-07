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

#include "Storage.hpp"

#include <Config.hpp>
#include <Logging.hpp>
#include <MiniTrace.hpp>
#include <TemporaryFile.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <core/Statistic.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <storage/secondary/FileStorage.hpp>
#include <storage/secondary/HttpStorage.hpp>
#ifdef HAVE_REDIS_STORAGE_BACKEND
#  include <storage/secondary/RedisStorage.hpp>
#endif
#include <util/Timer.hpp>
#include <util/Tokenizer.hpp>
#include <util/XXH3_64.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <third_party/url.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace storage {

const std::unordered_map<std::string /*scheme*/,
                         std::shared_ptr<secondary::SecondaryStorage>>
  k_secondary_storage_implementations = {
    {"file", std::make_shared<secondary::FileStorage>()},
    {"http", std::make_shared<secondary::HttpStorage>()},
#ifdef HAVE_REDIS_STORAGE_BACKEND
    {"redis", std::make_shared<secondary::RedisStorage>()},
    {"redis+unix", std::make_shared<secondary::RedisStorage>()},
#endif
};

std::string
get_features()
{
  std::vector<std::string> features;
  features.reserve(k_secondary_storage_implementations.size());
  std::transform(k_secondary_storage_implementations.begin(),
                 k_secondary_storage_implementations.end(),
                 std::back_inserter(features),
                 [](auto& entry) { return FMT("{}-storage", entry.first); });
  std::sort(features.begin(), features.end());
  return util::join(features, " ");
}

struct SecondaryStorageShardConfig
{
  std::string name;
  double weight;
};

struct SecondaryStorageConfig
{
  std::vector<SecondaryStorageShardConfig> shards;
  secondary::SecondaryStorage::Backend::Params params;
  bool read_only = false;
  bool share_hits = true;
};

struct SecondaryStorageBackendEntry
{
  Url url;                     // With expanded "*".
  std::string url_for_logging; // With expanded "*".
  std::unique_ptr<secondary::SecondaryStorage::Backend> impl;
  bool failed = false;
};

struct SecondaryStorageEntry
{
  SecondaryStorageConfig config;
  std::string url_for_logging; // With unexpanded "*".
  std::shared_ptr<secondary::SecondaryStorage> storage;
  std::vector<SecondaryStorageBackendEntry> backends;
};

static std::string
to_string(const SecondaryStorageConfig& entry)
{
  std::string result = entry.params.url.str();
  for (const auto& attr : entry.params.attributes) {
    result += FMT("|{}={}", attr.key, attr.raw_value);
  }
  return result;
}

static SecondaryStorageConfig
parse_storage_config(const std::string_view entry)
{
  const auto parts =
    Util::split_into_views(entry, "|", util::Tokenizer::Mode::include_empty);

  if (parts.empty() || parts.front().empty()) {
    throw core::Error("secondary storage config must provide a URL: {}", entry);
  }

  SecondaryStorageConfig result;
  const auto url_str = std::string(parts[0]);
  result.params.url = url_str;

  // The Url class is parsing the URL object lazily. Check if the URL is valid
  // now to avoid exceptions later.
  try {
    std::ignore = result.params.url.str();
  } catch (const std::exception& e) {
    throw core::Error("Cannot parse URL {}: {}", url_str, e.what());
  }

  if (result.params.url.scheme().empty()) {
    throw core::Error("URL scheme must not be empty: {}", entry);
  }

  for (size_t i = 1; i < parts.size(); ++i) {
    if (parts[i].empty()) {
      continue;
    }
    const auto [key, right_hand_side] = util::split_once(parts[i], '=');
    const auto& raw_value = right_hand_side.value_or("true");
    const auto value =
      util::value_or_throw<core::Error>(util::percent_decode(raw_value));
    if (key == "read-only") {
      result.read_only = (value == "true");
    } else if (key == "shards") {
      if (url_str.find('*') == std::string::npos) {
        throw core::Error(R"(Missing "*" in URL when using shards: "{}")",
                          url_str);
      }
      for (const auto& shard : util::Tokenizer(value, ",")) {
        double weight = 1.0;
        std::string_view name;
        const auto lp_pos = shard.find('(');
        if (lp_pos != std::string_view::npos) {
          if (shard.back() != ')') {
            throw core::Error("Invalid shard name: \"{}\"", shard);
          }
          weight =
            util::value_or_throw<core::Error>(util::parse_double(std::string(
              shard.substr(lp_pos + 1, shard.length() - lp_pos - 2))));
          if (weight < 0.0) {
            throw core::Error("Invalid shard weight: \"{}\"", weight);
          }
          name = shard.substr(0, lp_pos);
        } else {
          name = shard;
        }

        result.shards.push_back({std::string(name), weight});
      }
    } else if (key == "share-hits") {
      result.share_hits = (value == "true");
    }

    result.params.attributes.push_back(
      {std::string(key), value, std::string(raw_value)});
  }

  return result;
}

static std::vector<SecondaryStorageConfig>
parse_storage_configs(const std::string_view& configs)
{
  std::vector<SecondaryStorageConfig> result;
  for (const auto& config : util::Tokenizer(configs, " ")) {
    result.push_back(parse_storage_config(config));
  }
  return result;
}

static std::shared_ptr<secondary::SecondaryStorage>
get_storage(const Url& url)
{
  const auto it = k_secondary_storage_implementations.find(url.scheme());
  if (it != k_secondary_storage_implementations.end()) {
    return it->second;
  } else {
    return {};
  }
}

Storage::Storage(const Config& config) : primary(config), m_config(config)
{
}

Storage::~Storage()
{
  for (const auto& tmp_file : m_tmp_files) {
    Util::unlink_tmp(tmp_file);
  }
}

void
Storage::initialize()
{
  add_secondary_storages();
}

void
Storage::finalize()
{
  primary.finalize();
}

std::optional<std::string>
Storage::get(const Digest& key,
             const core::CacheEntryType type,
             const Mode mode)
{
  MTR_SCOPE("storage", "get");

  if (mode != Mode::secondary_only) {
    auto path = primary.get(key, type);
    primary.increment_statistic(path ? core::Statistic::primary_storage_hit
                                     : core::Statistic::primary_storage_miss);
    if (path) {
      if (m_config.reshare()) {
        // Temporary optimization until primary storage API has been refactored
        // to pass data via memory instead of files.
        const bool should_put_in_secondary_storage = std::any_of(
          m_secondary_storages.begin(),
          m_secondary_storages.end(),
          [](const auto& entry) { return !entry->config.read_only; });
        if (should_put_in_secondary_storage) {
          std::string value;
          try {
            value = Util::read_file(*path);
          } catch (const core::Error& e) {
            LOG("Failed to read {}: {}", *path, e.what());
            return path; // Don't indicate failure since primary storage was OK.
          }
          put_in_secondary_storage(key, value, true);
        }
      }

      return path;
    }
  }

  if (mode == Mode::primary_only) {
    return std::nullopt;
  }

  const auto value_and_share_hits = get_from_secondary_storage(key);
  if (!value_and_share_hits) {
    return std::nullopt;
  }
  const auto& value = value_and_share_hits->first;
  const auto& share_hits = value_and_share_hits->second;

  TemporaryFile tmp_file(FMT("{}/tmp.get", m_config.temporary_dir()));
  m_tmp_files.push_back(tmp_file.path);
  try {
    Util::write_file(tmp_file.path, value);
  } catch (const core::Error& e) {
    throw core::Fatal("Error writing to {}: {}", tmp_file.path, e.what());
  }

  if (share_hits) {
    primary.put(key, type, [&](const auto& path) {
      try {
        Util::ensure_dir_exists(Util::dir_name(path));
        Util::copy_file(tmp_file.path, path);
      } catch (const core::Error& e) {
        LOG("Failed to copy {} to {}: {}", tmp_file.path, path, e.what());
        // Don't indicate failure since get from primary storage was OK.
      }
      return true;
    });
  }

  return tmp_file.path;
}

bool
Storage::put(const Digest& key,
             const core::CacheEntryType type,
             const storage::EntryWriter& entry_writer)
{
  MTR_SCOPE("storage", "put");

  const auto path = primary.put(key, type, entry_writer);
  if (!path) {
    return false;
  }

  // Temporary optimization until primary storage API has been refactored to
  // pass data via memory instead of files.
  const bool should_put_in_secondary_storage =
    std::any_of(m_secondary_storages.begin(),
                m_secondary_storages.end(),
                [](const auto& entry) { return !entry->config.read_only; });
  if (should_put_in_secondary_storage) {
    std::string value;
    try {
      value = Util::read_file(*path);
    } catch (const core::Error& e) {
      LOG("Failed to read {}: {}", *path, e.what());
      return true; // Don't indicate failure since primary storage was OK.
    }
    put_in_secondary_storage(key, value, false);
  }

  return true;
}

void
Storage::remove(const Digest& key, const core::CacheEntryType type)
{
  MTR_SCOPE("storage", "remove");

  primary.remove(key, type);
  remove_from_secondary_storage(key);
}

bool
Storage::has_secondary_storage() const
{
  return !m_secondary_storages.empty();
}

std::string
Storage::get_secondary_storage_config_for_logging() const
{
  auto configs = parse_storage_configs(m_config.secondary_storage());
  for (auto& config : configs) {
    const auto storage = get_storage(config.params.url);
    if (storage) {
      storage->redact_secrets(config.params);
    }
  }
  return util::join(configs, " ");
}

static void
redact_url_for_logging(Url& url_for_logging)
{
  url_for_logging.user_info("");
}

void
Storage::add_secondary_storages()
{
  const auto configs = parse_storage_configs(m_config.secondary_storage());
  for (const auto& config : configs) {
    auto url_for_logging = config.params.url;
    redact_url_for_logging(url_for_logging);
    const auto storage = get_storage(config.params.url);
    if (!storage) {
      throw core::Error("unknown secondary storage URL: {}",
                        url_for_logging.str());
    }
    m_secondary_storages.push_back(std::make_unique<SecondaryStorageEntry>(
      SecondaryStorageEntry{config, url_for_logging.str(), storage, {}}));
  }
}

void
Storage::mark_backend_as_failed(
  SecondaryStorageBackendEntry& backend_entry,
  const secondary::SecondaryStorage::Backend::Failure failure)
{
  // The backend is expected to log details about the error.
  backend_entry.failed = true;
  primary.increment_statistic(
    failure == secondary::SecondaryStorage::Backend::Failure::timeout
      ? core::Statistic::secondary_storage_timeout
      : core::Statistic::secondary_storage_error);
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
get_shard_url(const Digest& key,
              const std::string& url,
              const std::vector<SecondaryStorageShardConfig>& shards)
{
  ASSERT(!shards.empty());

  // This is the "weighted rendezvous hashing" algorithm.
  double highest_score = -1.0;
  std::string best_shard;
  for (const auto& shard_config : shards) {
    util::XXH3_64 hash;
    hash.update(key.bytes(), key.size());
    hash.update(shard_config.name.data(), shard_config.name.length());
    const double score = to_half_open_unit_interval(hash.digest());
    ASSERT(score >= 0.0 && score < 1.0);
    const double weighted_score =
      score == 0.0 ? 0.0 : shard_config.weight / -std::log(score);
    if (weighted_score > highest_score) {
      best_shard = shard_config.name;
      highest_score = weighted_score;
    }
  }

  return util::replace_first(url, "*", best_shard);
}

SecondaryStorageBackendEntry*
Storage::get_backend(SecondaryStorageEntry& entry,
                     const Digest& key,
                     const std::string_view operation_description,
                     const bool for_writing)
{
  if (for_writing && entry.config.read_only) {
    LOG("Not {} {} since it is read-only",
        operation_description,
        entry.url_for_logging);
    return nullptr;
  }

  const auto shard_url =
    entry.config.shards.empty()
      ? entry.config.params.url
      : get_shard_url(key, entry.config.params.url.str(), entry.config.shards);
  auto backend =
    std::find_if(entry.backends.begin(),
                 entry.backends.end(),
                 [&](const auto& x) { return x.url.str() == shard_url.str(); });

  if (backend == entry.backends.end()) {
    auto shard_url_for_logging = shard_url;
    redact_url_for_logging(shard_url_for_logging);
    entry.backends.push_back(
      {shard_url, shard_url_for_logging.str(), {}, false});
    auto shard_params = entry.config.params;
    shard_params.url = shard_url;
    try {
      entry.backends.back().impl = entry.storage->create_backend(shard_params);
    } catch (const secondary::SecondaryStorage::Backend::Failed& e) {
      LOG("Failed to construct backend for {}{}",
          entry.url_for_logging,
          std::string_view(e.what()).empty() ? "" : FMT(": {}", e.what()));
      mark_backend_as_failed(entry.backends.back(), e.failure());
      return nullptr;
    }
    return &entry.backends.back();
  } else if (backend->failed) {
    LOG("Not {} {} since it failed earlier",
        operation_description,
        entry.url_for_logging);
    return nullptr;
  } else {
    return &*backend;
  }
}

std::optional<std::pair<std::string, bool>>
Storage::get_from_secondary_storage(const Digest& key)
{
  MTR_SCOPE("secondary_storage", "get");

  for (const auto& entry : m_secondary_storages) {
    auto backend = get_backend(*entry, key, "getting from", false);
    if (!backend) {
      continue;
    }

    Timer timer;
    const auto result = backend->impl->get(key);
    const auto ms = timer.measure_ms();
    if (!result) {
      mark_backend_as_failed(*backend, result.error());
      continue;
    }

    const auto& value = *result;
    if (value) {
      LOG("Retrieved {} from {} ({:.2f} ms)",
          key.to_string(),
          backend->url_for_logging,
          ms);
      primary.increment_statistic(core::Statistic::secondary_storage_hit);
      return std::make_pair(*value, entry->config.share_hits);
    } else {
      LOG("No {} in {} ({:.2f} ms)",
          key.to_string(),
          backend->url_for_logging,
          ms);
      primary.increment_statistic(core::Statistic::secondary_storage_miss);
    }
  }

  return std::nullopt;
}

void
Storage::put_in_secondary_storage(const Digest& key,
                                  const std::string& value,
                                  bool only_if_missing)
{
  MTR_SCOPE("secondary_storage", "put");

  for (const auto& entry : m_secondary_storages) {
    auto backend = get_backend(*entry, key, "putting in", true);
    if (!backend) {
      continue;
    }

    Timer timer;
    const auto result = backend->impl->put(key, value, only_if_missing);
    const auto ms = timer.measure_ms();
    if (!result) {
      // The backend is expected to log details about the error.
      mark_backend_as_failed(*backend, result.error());
      continue;
    }

    const bool stored = *result;
    LOG("{} {} in {} ({:.2f} ms)",
        stored ? "Stored" : "Did not have to store",
        key.to_string(),
        backend->url_for_logging,
        ms);
  }
}

void
Storage::remove_from_secondary_storage(const Digest& key)
{
  MTR_SCOPE("secondary_storage", "remove");

  for (const auto& entry : m_secondary_storages) {
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
          key.to_string(),
          backend->url_for_logging,
          ms);
    } else {
      LOG("No {} to remove from {} ({:.2f} ms)",
          key.to_string(),
          backend->url_for_logging,
          ms);
    }
  }
}

} // namespace storage
