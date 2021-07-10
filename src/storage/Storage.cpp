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
#include <util/Tokenizer.hpp>
#include <util/string_utils.hpp>

#include <third_party/url.hpp>

#include <memory>
#include <tuple>

namespace storage {

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

primary::PrimaryStorage&
Storage::primary()
{
  return m_primary_storage;
}

nonstd::optional<std::string>
Storage::get(const Digest& key, const core::CacheEntryType type)
{
  const auto path = m_primary_storage.get(key, type);
  if (path) {
    return path;
  }

  for (const auto& storage : m_secondary_storages) {
    const auto result = storage.backend->get(key);
    if (!result) {
      // The backend is expected to log details about the error.
      // TODO: Update statistics.
      continue;
    }

    const auto& value = *result;
    if (!value) {
      LOG("No {} in {}", key.to_string(), storage.url);
      continue;
    }

    TemporaryFile tmp_file(FMT("{}/tmp.get", m_config.temporary_dir()));
    m_tmp_files.push_back(tmp_file.path);
    try {
      Util::write_file(tmp_file.path, *value);
    } catch (const Error& e) {
      throw Fatal("Error writing to {}: {}", tmp_file.path, e.what());
    }

    LOG("Retrieved {} from {}", key.to_string(), storage.url);
    return tmp_file.path;
  }

  return nonstd::nullopt;
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

  nonstd::optional<std::string> value;
  for (const auto& storage : m_secondary_storages) {
    if (storage.read_only) {
      LOG("Not storing {} in {} since it is read-only",
          key.to_string(),
          storage.url);
      continue;
    }

    if (!value) {
      try {
        value = Util::read_file(*path);
      } catch (const Error& e) {
        LOG("Failed to read {}: {}", *path, e.what());
        break; // Don't indicate failure since primary storage was OK.
      }
    }

    const auto result = storage.backend->put(key, *value);
    if (!result) {
      // The backend is expected to log details about the error.
      // TODO: Update statistics.
      continue;
    }

    const bool stored = *result;
    LOG("{} {} in {}",
        stored ? "Stored" : "Failed to store",
        key.to_string(),
        storage.url);
  }

  return true;
}

void
Storage::remove(const Digest& key, const core::CacheEntryType type)
{
  m_primary_storage.remove(key, type);

  for (const auto& storage : m_secondary_storages) {
    if (storage.read_only) {
      LOG("Did not remove {} from {} since it is read-only",
          key.to_string(),
          storage.url);
      continue;
    }

    const auto result = storage.backend->remove(key);
    if (!result) {
      // The backend is expected to log details about the error.
      // TODO: Update statistics.
      continue;
    }

    const bool removed = *result;
    if (removed) {
      LOG("Removed {} from {}", key.to_string(), storage.url);
    } else {
      LOG("No {} to remove from {}", key.to_string(), storage.url);
    }
  }
}

namespace {

struct ParseStorageEntryResult
{
  Url url;
  storage::AttributeMap attributes;
  bool read_only = false;
};

} // namespace

static ParseStorageEntryResult
parse_storage_entry(const nonstd::string_view& entry)
{
  const auto parts =
    Util::split_into_views(entry, "|", util::Tokenizer::Mode::include_empty);

  if (parts.empty() || parts.front().empty()) {
    throw Error("secondary storage config must provide a URL: {}", entry);
  }

  ParseStorageEntryResult result;
  result.url = std::string(parts[0]);
  // Url class is parsing the URL object lazily; check if successful
  try {
    std::ignore = result.url.host();
  } catch (Url::parse_error& e) {
    throw Error("Cannot parse URL: {}", e.what());
  }

  if (result.url.scheme().empty()) {
    throw Error("URL scheme must not be empty: {}", entry);
  }

  for (size_t i = 1; i < parts.size(); ++i) {
    if (parts[i].empty()) {
      continue;
    }
    const auto kv_pair = util::split_once(parts[i], '=');
    const auto& key = kv_pair.first;
    const auto& value = kv_pair.second ? *kv_pair.second : "true";
    const auto decoded_value = util::percent_decode(value);
    if (!decoded_value) {
      throw Error(decoded_value.error());
    }
    if (key == "read-only" && value == "true") {
      result.read_only = true;
    } else {
      result.attributes.emplace(std::string(key), *decoded_value);
    }
  }

  return result;
}

static std::unique_ptr<SecondaryStorage>
create_storage(const ParseStorageEntryResult& storage_entry)
{
  if (storage_entry.url.scheme() == "file") {
    return std::make_unique<secondary::FileStorage>(storage_entry.url,
                                                    storage_entry.attributes);
  } else if (storage_entry.url.scheme() == "http") {
    return std::make_unique<secondary::HttpStorage>(storage_entry.url,
                                                    storage_entry.attributes);
  }

#ifdef HAVE_REDIS_STORAGE_BACKEND
  if (storage_entry.url.scheme() == "redis") {
    return std::make_unique<secondary::RedisStorage>(storage_entry.url,
                                                     storage_entry.attributes);
  }
#endif

  return {};
}

void
Storage::add_secondary_storages()
{
  for (const auto& entry : util::Tokenizer(m_config.secondary_storage(), " ")) {
    const auto storage_entry = parse_storage_entry(entry);
    auto storage = create_storage(storage_entry);
    auto url_for_logging = storage_entry.url.str();
    if (!storage) {
      throw Error("unknown secondary storage URL: {}", url_for_logging);
    }
    m_secondary_storages.push_back(SecondaryStorageEntry{
      std::move(storage), url_for_logging, storage_entry.read_only});
  }
}

} // namespace storage
