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

#include "LocalStorage.hpp"

#include <AtomicFile.hpp>
#include <Config.hpp>
#include <Logging.hpp>
#include <MiniTrace.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <fmtmacros.hpp>
#include <storage/local/StatsFile.hpp>
#include <util/Duration.hpp>
#include <util/file.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

using core::Statistic;

namespace storage::local {

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
const util::Duration k_tempdir_cleanup_interval(2 * 24 * 60 * 60); // 2 days

// Maximum files per cache directory. This constant is somewhat arbitrarily
// chosen to be large enough to avoid unnecessary cache levels but small enough
// not to make esoteric file systems (with bad performance for large
// directories) too slow. It could be made configurable, but hopefully there
// will be no need to do that.
const uint64_t k_max_cache_files_per_directory = 2000;

// Minimum number of cache levels ($CCACHE_DIR/1/2/stored_file).
const uint8_t k_min_cache_levels = 2;

// Maximum number of cache levels ($CCACHE_DIR/1/2/3/stored_file).
//
// On a cache miss, (k_max_cache_levels - k_min_cache_levels + 1) cache lookups
// (i.e. stat system calls) will be performed for a cache entry.
//
// An assumption made here is that if a cache is so large that it holds more
// than 16^4 * k_max_cache_files_per_directory files then we can assume that the
// file system is sane enough to handle more than
// k_max_cache_files_per_directory.
const uint8_t k_max_cache_levels = 4;

static std::string
suffix_from_type(const core::CacheEntryType type)
{
  switch (type) {
  case core::CacheEntryType::manifest:
    return "M";

  case core::CacheEntryType::result:
    return "R";
  }

  ASSERT(false);
}

static uint8_t
calculate_wanted_cache_level(const uint64_t files_in_level_1)
{
  uint64_t files_per_directory = files_in_level_1 / 16;
  for (uint8_t i = k_min_cache_levels; i <= k_max_cache_levels; ++i) {
    if (files_per_directory < k_max_cache_files_per_directory) {
      return i;
    }
    files_per_directory /= 16;
  }
  return k_max_cache_levels;
}

LocalStorage::LocalStorage(const Config& config) : m_config(config)
{
}

void
LocalStorage::finalize()
{
  if (m_config.temporary_dir() == m_config.default_temporary_dir()) {
    clean_internal_tempdir();
  }

  if (!m_config.stats()) {
    return;
  }

  if (m_manifest_key) {
    // A manifest entry was written.
    ASSERT(!m_manifest_path.empty());
    update_stats_and_maybe_move_cache_file(*m_manifest_key,
                                           m_manifest_path,
                                           m_manifest_counter_updates,
                                           core::CacheEntryType::manifest);
  }

  if (!m_result_key) {
    // No result entry was written, so we just choose one of the stats files in
    // the 256 level 2 directories.

    ASSERT(m_result_counter_updates.get(Statistic::cache_size_kibibyte) == 0);
    ASSERT(m_result_counter_updates.get(Statistic::files_in_cache) == 0);

    const auto bucket = getpid() % 256;
    const auto stats_file =
      FMT("{}/{:x}/{:x}/stats", m_config.cache_dir(), bucket / 16, bucket % 16);
    StatsFile(stats_file).update([&](auto& cs) {
      cs.increment(m_result_counter_updates);
    });
    return;
  }

  ASSERT(!m_result_path.empty());

  const auto counters =
    update_stats_and_maybe_move_cache_file(*m_result_key,
                                           m_result_path,
                                           m_result_counter_updates,
                                           core::CacheEntryType::result);
  if (!counters) {
    return;
  }

  const auto subdir =
    FMT("{}/{:x}", m_config.cache_dir(), m_result_key->bytes()[0] >> 4);
  bool need_cleanup = false;

  if (m_config.max_files() != 0
      && counters->get(Statistic::files_in_cache) > m_config.max_files() / 16) {
    LOG("Need to clean up {} since it holds {} files (limit: {} files)",
        subdir,
        counters->get(Statistic::files_in_cache),
        m_config.max_files() / 16);
    need_cleanup = true;
  }
  if (m_config.max_size() != 0
      && counters->get(Statistic::cache_size_kibibyte)
           > m_config.max_size() / 1024 / 16) {
    LOG("Need to clean up {} since it holds {} KiB (limit: {} KiB)",
        subdir,
        counters->get(Statistic::cache_size_kibibyte),
        m_config.max_size() / 1024 / 16);
    need_cleanup = true;
  }

  if (need_cleanup) {
    const double factor = m_config.limit_multiple() / 16;
    const uint64_t max_size = round(m_config.max_size() * factor);
    const uint32_t max_files = round(m_config.max_files() * factor);
    clean_dir(subdir,
              max_size,
              max_files,
              std::nullopt,
              std::nullopt,
              [](double /*progress*/) {});
  }
}

std::optional<util::Bytes>
LocalStorage::get(const Digest& key, const core::CacheEntryType type) const
{
  MTR_SCOPE("local_storage", "get");

  const auto cache_file = look_up_cache_file(key, type);
  if (!cache_file.stat) {
    LOG("No {} {} in local storage", key.to_string(), core::to_string(type));
    return std::nullopt;
  }
  const auto value = util::read_file<util::Bytes>(cache_file.path);
  if (!value) {
    LOG("Failed to read {}: {}", cache_file.path, value.error());
    return std::nullopt;
  }

  LOG("Retrieved {} from local storage ({})", key.to_string(), cache_file.path);

  // Update modification timestamp to save file from LRU cleanup.
  util::set_timestamps(cache_file.path);

  return *value;
}

void
LocalStorage::put(const Digest& key,
                  const core::CacheEntryType type,
                  nonstd::span<const uint8_t> value,
                  bool only_if_missing)
{
  MTR_SCOPE("local_storage", "put");

  const auto cache_file = look_up_cache_file(key, type);
  if (only_if_missing && cache_file.stat) {
    LOG("Not storing {} in local storage since it already exists",
        cache_file.path);
    return;
  }

  switch (type) {
  case core::CacheEntryType::manifest:
    m_manifest_key = key;
    m_manifest_path = cache_file.path;
    break;

  case core::CacheEntryType::result:
    m_result_key = key;
    m_result_path = cache_file.path;
    break;
  }

  try {
    AtomicFile result_file(cache_file.path, AtomicFile::Mode::binary);
    result_file.write(value);
    result_file.commit();
  }

  catch (core::Error& e) {
    LOG("Failed to write to {}: {}", cache_file.path, e.what());
    return;
  }

  const auto new_stat = Stat::stat(cache_file.path, Stat::OnError::log);
  if (!new_stat) {
    LOG("Failed to stat {}: {}", cache_file.path, strerror(errno));
    return;
  }

  LOG("Stored {} in local storage ({})", key.to_string(), cache_file.path);

  auto& counter_updates = (type == core::CacheEntryType::manifest)
                            ? m_manifest_counter_updates
                            : m_result_counter_updates;
  counter_updates.increment(
    Statistic::cache_size_kibibyte,
    Util::size_change_kibibyte(cache_file.stat, new_stat));
  counter_updates.increment(Statistic::files_in_cache, cache_file.stat ? 0 : 1);

  // Make sure we have a CACHEDIR.TAG in the cache part of cache_dir. This can
  // be done almost anywhere, but we might as well do it near the end as we save
  // the stat call if we exit early.
  util::create_cachedir_tag(
    FMT("{}/{}", m_config.cache_dir(), key.to_string()[0]));
}

void
LocalStorage::remove(const Digest& key, const core::CacheEntryType type)
{
  MTR_SCOPE("local_storage", "remove");

  const auto cache_file = look_up_cache_file(key, type);
  if (cache_file.stat) {
    Util::unlink_safe(cache_file.path);
    LOG("Removed {} from local storage ({})", key.to_string(), cache_file.path);
  } else {
    LOG("No {} to remove from local storage", key.to_string());
  }
}

std::string
LocalStorage::get_raw_file_path(std::string_view result_path,
                                uint8_t file_number)
{
  if (file_number >= 10) {
    // To support more entries in the future, encode to [0-9a-z]. Note that
    // LocalStorage::evict currently assumes that the entry number is
    // represented as one character.
    throw core::Error(FMT("Too high raw file entry number: {}", file_number));
  }

  const auto prefix = result_path.substr(0, result_path.length() - 1);
  return FMT("{}{}W", prefix, file_number);
}

std::string
LocalStorage::get_raw_file_path(const Digest& result_key,
                                uint8_t file_number) const
{
  const auto cache_file =
    look_up_cache_file(result_key, core::CacheEntryType::result);
  return get_raw_file_path(cache_file.path, file_number);
}

void
LocalStorage::put_raw_files(
  const Digest& key,
  const std::vector<core::Result::Serializer::RawFile> raw_files)
{
  const auto cache_file = look_up_cache_file(key, core::CacheEntryType::result);
  Util::ensure_dir_exists(Util::dir_name(cache_file.path));

  for (auto [file_number, source_path] : raw_files) {
    const auto dest_path = get_raw_file_path(cache_file.path, file_number);
    const auto old_stat = Stat::stat(dest_path);
    try {
      Util::clone_hard_link_or_copy_file(
        m_config, source_path, dest_path, true);
      m_added_raw_files.push_back(dest_path);
    } catch (core::Error& e) {
      LOG("Failed to store {} as raw file {}: {}",
          source_path,
          dest_path,
          e.what());
      throw;
    }
    const auto new_stat = Stat::stat(dest_path);
    increment_statistic(Statistic::cache_size_kibibyte,
                        Util::size_change_kibibyte(old_stat, new_stat));
    increment_statistic(Statistic::files_in_cache,
                        (new_stat ? 1 : 0) - (old_stat ? 1 : 0));
  }
}

void
LocalStorage::increment_statistic(const Statistic statistic,
                                  const int64_t value)
{
  m_result_counter_updates.increment(statistic, value);
}

void
LocalStorage::increment_statistics(const core::StatisticsCounters& statistics)
{
  m_result_counter_updates.increment(statistics);
}

// Private methods

LocalStorage::LookUpCacheFileResult
LocalStorage::look_up_cache_file(const Digest& key,
                                 const core::CacheEntryType type) const
{
  const auto key_string = FMT("{}{}", key.to_string(), suffix_from_type(type));

  for (uint8_t level = k_min_cache_levels; level <= k_max_cache_levels;
       ++level) {
    const auto path = get_path_in_cache(level, key_string);
    const auto stat = Stat::stat(path);
    if (stat) {
      return {path, stat, level};
    }
  }

  const auto shallowest_path =
    get_path_in_cache(k_min_cache_levels, key_string);
  return {shallowest_path, Stat(), k_min_cache_levels};
}

void
LocalStorage::clean_internal_tempdir()
{
  MTR_SCOPE("local_storage", "clean_internal_tempdir");

  const auto now = util::TimePoint::now();
  const auto cleaned_stamp = FMT("{}/.cleaned", m_config.temporary_dir());
  const auto cleaned_stat = Stat::stat(cleaned_stamp);
  if (cleaned_stat
      && cleaned_stat.mtime() + k_tempdir_cleanup_interval >= now) {
    // No cleanup needed.
    return;
  }

  LOG("Cleaning up {}", m_config.temporary_dir());
  Util::ensure_dir_exists(m_config.temporary_dir());
  Util::traverse(m_config.temporary_dir(),
                 [now](const std::string& path, bool is_dir) {
                   if (is_dir) {
                     return;
                   }
                   const auto st = Stat::lstat(path, Stat::OnError::log);
                   if (st && st.mtime() + k_tempdir_cleanup_interval < now) {
                     Util::unlink_tmp(path);
                   }
                 });

  util::write_file(cleaned_stamp, "");
}

std::optional<core::StatisticsCounters>
LocalStorage::update_stats_and_maybe_move_cache_file(
  const Digest& key,
  const std::string& current_path,
  const core::StatisticsCounters& counter_updates,
  const core::CacheEntryType type)
{
  if (counter_updates.all_zero()) {
    return std::nullopt;
  }

  // Use stats file in the level one subdirectory for cache bookkeeping counters
  // since cleanup is performed on level one. Use stats file in the level two
  // subdirectory for other counters to reduce lock contention.
  const bool use_stats_on_level_1 =
    counter_updates.get(Statistic::cache_size_kibibyte) != 0
    || counter_updates.get(Statistic::files_in_cache) != 0;
  std::string level_string = FMT("{:x}", key.bytes()[0] >> 4);
  if (!use_stats_on_level_1) {
    level_string += FMT("/{:x}", key.bytes()[0] & 0xF);
  }

  const auto stats_file =
    FMT("{}/{}/stats", m_config.cache_dir(), level_string);
  auto counters = StatsFile(stats_file).update([&counter_updates](auto& cs) {
    cs.increment(counter_updates);
  });
  if (!counters) {
    return std::nullopt;
  }

  if (use_stats_on_level_1) {
    // Only consider moving the cache file to another level when we have read
    // the level 1 stats file since it's only then we know the proper
    // files_in_cache value.
    const auto wanted_level =
      calculate_wanted_cache_level(counters->get(Statistic::files_in_cache));
    const auto wanted_path =
      get_path_in_cache(wanted_level, key.to_string() + suffix_from_type(type));
    if (current_path != wanted_path) {
      Util::ensure_dir_exists(Util::dir_name(wanted_path));
      LOG("Moving {} to {}", current_path, wanted_path);
      try {
        Util::rename(current_path, wanted_path);
      } catch (const core::Error&) {
        // Two ccache processes may move the file at the same time, so failure
        // to rename is OK.
      }
      for (const auto& raw_file : m_added_raw_files) {
        try {
          Util::rename(raw_file,
                       FMT("{}/{}",
                           Util::dir_name(wanted_path),
                           Util::base_name(raw_file)));
        } catch (const core::Error&) {
          // Two ccache processes may move the file at the same time, so failure
          // to rename is OK.
        }
      }
    }
  }
  return counters;
}

std::string
LocalStorage::get_path_in_cache(const uint8_t level,
                                const std::string_view name) const
{
  ASSERT(level >= 1 && level <= 8);
  ASSERT(name.length() >= level);

  std::string path(m_config.cache_dir());
  path.reserve(path.size() + level * 2 + 1 + name.length() - level);

  for (uint8_t i = 0; i < level; ++i) {
    path.push_back('/');
    path.push_back(name.at(i));
  }

  path.push_back('/');
  const std::string_view name_remaining = name.substr(level);
  path.append(name_remaining.data(), name_remaining.length());

  return path;
}

} // namespace storage::local
