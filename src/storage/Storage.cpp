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

#include "Storage.hpp"

#include <Config.hpp>
#include <Logging.hpp>
#include <TemporaryFile.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <fmtmacros.hpp>
#include <storage/secondary/FileStorage.hpp>
#include <storage/secondary/HttpStorage.hpp>
#ifdef HAVE_REDIS_STORAGE_BACKEND
#  include <storage/secondary/RedisStorage.hpp>
#endif
#include <storage/secondary/SecondaryStorage.hpp>
#include <util/Tokenizer.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <third_party/url.hpp>

#include <algorithm>
#include <unordered_map>

namespace storage {

const std::unordered_map<std::string /*scheme*/,
                         std::shared_ptr<secondary::SecondaryStorage>>
  k_secondary_storage_implementations = {
    {"file", std::make_shared<secondary::FileStorage>()},
    {"http", std::make_shared<secondary::HttpStorage>()},
#ifdef HAVE_REDIS_STORAGE_BACKEND
    {"redis", std::make_shared<secondary::RedisStorage>()},
#endif
};

struct SecondaryStorageConfig
{
  secondary::SecondaryStorage::Backend::Params params;
  bool read_only = false;
};

struct SecondaryStorageEntry
{
  SecondaryStorageConfig config;
  std::string url_for_logging;
  std::shared_ptr<secondary::SecondaryStorage> storage;
  std::unique_ptr<secondary::SecondaryStorage::Backend> backend;
  bool failed = false;
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
parse_storage_config(const nonstd::string_view entry)
{
  const auto parts =
    Util::split_into_views(entry, "|", util::Tokenizer::Mode::include_empty);

  if (parts.empty() || parts.front().empty()) {
    throw Error("secondary storage config must provide a URL: {}", entry);
  }

  SecondaryStorageConfig result;
  result.params.url = to_string(parts[0]);
  // The Url class is parsing the URL object lazily; check if successful.
  try {
    std::ignore = result.params.url.host();
  } catch (const Url::parse_error& e) {
    throw Error("Cannot parse URL: {}", e.what());
  }

  if (result.params.url.scheme().empty()) {
    throw Error("URL scheme must not be empty: {}", entry);
  }

  for (size_t i = 1; i < parts.size(); ++i) {
    if (parts[i].empty()) {
      continue;
    }
    const auto kv_pair = util::split_once(parts[i], '=');
    const auto& key = kv_pair.first;
    const auto& raw_value = kv_pair.second.value_or("true");
    const auto value =
      util::value_or_throw<Error>(util::percent_decode(raw_value));
    if (key == "read-only" && value == "true") {
      result.read_only = true;
    }
    result.params.attributes.emplace_back(
      secondary::SecondaryStorage::Backend::Attribute{
        to_string(key), value, to_string(raw_value)});
  }

  return result;
}

static std::vector<SecondaryStorageConfig>
parse_storage_configs(const nonstd::string_view& configs)
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

Storage::Storage(const Config& config)
  : m_config(config),
    m_primary_storage(config)
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
  m_primary_storage.initialize();
  add_secondary_storages();
}

void
Storage::finalize()
{
  m_primary_storage.finalize();
}

nonstd::optional<std::string>
Storage::get(const Digest& key, const core::CacheEntryType type)
{
  const auto path = m_primary_storage.get(key, type);
  if (path) {
    return path;
  }

  const auto value = get_from_secondary_storage(key);
  if (!value) {
    return nonstd::nullopt;
  }

  TemporaryFile tmp_file(FMT("{}/tmp.get", m_config.temporary_dir()));
  m_tmp_files.push_back(tmp_file.path);
  try {
    Util::write_file(tmp_file.path, *value);
  } catch (const Error& e) {
    throw Fatal("Error writing to {}: {}", tmp_file.path, e.what());
  }
  return tmp_file.path;
}

bool
Storage::put(const Digest& key,
             const core::CacheEntryType type,
             const storage::CacheEntryWriter& entry_writer)
{
  const auto path = m_primary_storage.put(key, type, entry_writer);
  if (!path) {
    return false;
  }

  // Temporary optimization until primary storage API has been refactored to
  // pass data via memory instead of files.
  const bool should_put_in_secondary_storage =
    std::any_of(m_secondary_storages.begin(),
                m_secondary_storages.end(),
                [](const auto& entry) {
                  return !entry->failed && !entry->config.read_only;
                });
  if (should_put_in_secondary_storage) {
    std::string value;
    try {
      value = Util::read_file(*path);
    } catch (const Error& e) {
      LOG("Failed to read {}: {}", *path, e.what());
      return true; // Don't indicate failure since primary storage was OK.
    }
    put_in_secondary_storage(key, value);
  }

  return true;
}

void
Storage::remove(const Digest& key, const core::CacheEntryType type)
{
  m_primary_storage.remove(key, type);
  remove_from_secondary_storage(key);
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

void
Storage::add_secondary_storages()
{
  const auto configs = parse_storage_configs(m_config.secondary_storage());
  for (const auto& config : configs) {
    auto url_for_logging = config.params.url;
    url_for_logging.user_info("");
    const auto storage = get_storage(config.params.url);
    if (!storage) {
      throw Error("unknown secondary storage URL: {}", url_for_logging.str());
    }
    m_secondary_storages.emplace_back(
      std::make_unique<SecondaryStorageEntry>(SecondaryStorageEntry{
        config, url_for_logging.str(), storage, {}, false}));
  }
}

static void
mark_backend_as_failed(
  SecondaryStorageEntry& entry,
  const secondary::SecondaryStorage::Backend::Failure failure)
{
  // The backend is expected to log details about the error.
  entry.failed = true;
  (void)failure; // TODO: Update statistics.
}

static bool
backend_is_available(SecondaryStorageEntry& entry,
                     nonstd::string_view operation_description,
                     const bool for_writing)
{
  if (for_writing && entry.config.read_only) {
    LOG("Not {} {} since it is read-only",
        operation_description,
        entry.url_for_logging);
    return false;
  }

  if (entry.failed) {
    LOG("Not {} {} since it failed earlier",
        operation_description,
        entry.url_for_logging);
    return false;
  }

  try {
    entry.backend = entry.storage->create_backend(entry.config.params);
  } catch (const secondary::SecondaryStorage::Backend::Failed& e) {
    LOG("Failed to construct backend for {}{}",
        entry.url_for_logging,
        nonstd::string_view(e.what()).empty() ? "" : FMT(": {}", e.what()));
    mark_backend_as_failed(entry, e.failure());
    return false;
  }

  return true;
}

nonstd::optional<std::string>
Storage::get_from_secondary_storage(const Digest& key)
{
  for (const auto& entry : m_secondary_storages) {
    if (!backend_is_available(*entry, "getting from", false)) {
      continue;
    }

    const auto result = entry->backend->get(key);
    if (!result) {
      mark_backend_as_failed(*entry, result.error());
      continue;
    }

    const auto& value = *result;
    if (value) {
      LOG("Retrieved {} from {}", key.to_string(), entry->url_for_logging);
      return *value;
    } else {
      LOG("No {} in {}", key.to_string(), entry->url_for_logging);
    }
  }

  return nonstd::nullopt;
}

void
Storage::put_in_secondary_storage(const Digest& key, const std::string& value)
{
  for (const auto& entry : m_secondary_storages) {
    if (!backend_is_available(*entry, "putting in", true)) {
      continue;
    }

    const auto result = entry->backend->put(key, value);
    if (!result) {
      // The backend is expected to log details about the error.
      mark_backend_as_failed(*entry, result.error());
      continue;
    }

    const bool stored = *result;
    LOG("{} {} in {}",
        stored ? "Stored" : "Failed to store",
        key.to_string(),
        entry->url_for_logging);
  }
}

void
Storage::remove_from_secondary_storage(const Digest& key)
{
  for (const auto& entry : m_secondary_storages) {
    if (!backend_is_available(*entry, "removing from", true)) {
      continue;
    }

    const auto result = entry->backend->remove(key);
    if (!result) {
      mark_backend_as_failed(*entry, result.error());
      continue;
    }

    const bool removed = *result;
    if (removed) {
      LOG("Removed {} from {}", key.to_string(), entry->url_for_logging);
    } else {
      LOG("No {} to remove from {}", key.to_string(), entry->url_for_logging);
    }
  }
}

} // namespace storage
