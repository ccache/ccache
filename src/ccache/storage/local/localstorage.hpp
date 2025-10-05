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

#pragma once

#include <ccache/core/result.hpp>
#include <ccache/core/statistic.hpp>
#include <ccache/core/statisticscounters.hpp>
#include <ccache/core/types.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/local/statsfile.hpp>
#include <ccache/storage/local/util.hpp>
#include <ccache/storage/types.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/lockfile.hpp>
#include <ccache/util/longlivedlockfilemanager.hpp>
#include <ccache/util/time.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Config;

namespace storage::local {

struct CompressionStatistics
{
  // Storage that would be needed to store the content of compressible entries
  // uncompressed (without headers), rounded up to disk blocks.
  uint64_t content_size;
  // Actual size of compressible entries (including headers), rounded up to disk
  // blocks.
  uint64_t actual_size;
  // Actual size of incompressible entries, rounded up to disk blocks.
  uint64_t incompressible_size;
};

enum class FileType { result, manifest, raw, unknown };

FileType file_type_from_path(const std::filesystem::path& path);

class LocalStorage
{
public:
  LocalStorage(const Config& config);

  void finalize();

  // --- Cache entry handling ---

  std::optional<util::Bytes> get(const Hash::Digest& key,
                                 core::CacheEntryType type);

  void put(const Hash::Digest& key,
           core::CacheEntryType type,
           nonstd::span<const uint8_t> value,
           Overwrite overwrite);

  void remove(const Hash::Digest& key, core::CacheEntryType type);

  static std::filesystem::path
  get_raw_file_path(const std::filesystem::path& result_path,
                    uint8_t file_number);
  std::filesystem::path get_raw_file_path(const Hash::Digest& result_key,
                                          uint8_t file_number) const;

  void put_raw_files(
    const Hash::Digest& key,
    const std::vector<core::result::Serializer::RawFile>& raw_files);

  // Clone, hard link or copy a file from `source` to `dest` depending on
  // settings in `ctx`. If cloning or hard linking cannot and should not be done
  // the file will be copied instead. Throws `core::Error` on error.
  void clone_hard_link_or_copy_file(const std::filesystem::path& source,
                                    const std::filesystem::path& dest,
                                    bool via_tmp_file = false) const;

  // --- Statistics ---

  void increment_statistic(core::Statistic statistic, int64_t value = 1);
  void increment_statistics(const core::StatisticsCounters& statistics);

  const core::StatisticsCounters& get_statistics_updates() const;

  // Zero all statistics counters except those tracking cache size and number of
  // files in the cache.
  void zero_all_statistics();

  // Get statistics and last time of update for the whole local storage cache.
  std::pair<core::StatisticsCounters, util::TimePoint>
  get_all_statistics() const;

  // --- Cleanup ---

  void evict(const ProgressReceiver& progress_receiver,
             std::optional<uint64_t> max_age,
             std::optional<std::string> namespace_);

  void clean_all(const ProgressReceiver& progress_receiver);

  void wipe_all(const ProgressReceiver& progress_receiver);

  // --- Compression ---

  CompressionStatistics
  get_compression_statistics(const ProgressReceiver& progress_receiver) const;

  void recompress(std::optional<int8_t> level,
                  uint32_t threads,
                  const ProgressReceiver& progress_receiver);

private:
  const Config& m_config;

  // Statistics updates (excluding size/count changes) that will get written to
  // a statistics file in the finalize method.
  core::StatisticsCounters m_counter_updates;

  struct AddedRawFile
  {
    uint8_t file_number;
    std::filesystem::path dest_path;
  };
  std::vector<AddedRawFile> m_added_raw_files;
  bool m_stored_data = false;

  struct LookUpCacheFileResult
  {
    std::filesystem::path path;
    util::DirEntry dir_entry;
    uint8_t level;
  };

  LookUpCacheFileResult look_up_cache_file(const Hash::Digest& key,
                                           core::CacheEntryType type) const;

  std::filesystem::path get_subdir(uint8_t l1_index) const;
  std::filesystem::path get_subdir(uint8_t l1_index, uint8_t l2_index) const;

  StatsFile get_stats_file(uint8_t l1_index) const;
  StatsFile get_stats_file(uint8_t l1_index, uint8_t l2_index) const;

  void move_to_wanted_cache_level(const core::StatisticsCounters& counters,
                                  const Hash::Digest& key,
                                  core::CacheEntryType type,
                                  const std::filesystem::path& cache_file_path);

  void recount_level_1_dir(util::LongLivedLockFileManager& lock_manager,
                           uint8_t l1_index);

  std::optional<core::StatisticsCounters> increment_files_and_size_counters(
    uint8_t l1_index, uint8_t l2_index, int64_t files, int64_t size_kibibyte);
  std::optional<core::StatisticsCounters> increment_files_and_size_counters(
    const Hash::Digest& key, int64_t files, int64_t size_kibibyte);

  void perform_automatic_cleanup();

  void do_clean_all(const ProgressReceiver& progress_receiver,
                    uint64_t max_size,
                    uint64_t max_files,
                    std::optional<uint64_t> max_age,
                    std::optional<std::string> namespace_);

  struct EvaluateCleanupResult
  {
    uint8_t l1_index;
    std::filesystem::path l1_path;
    core::StatisticsCounters l1_counters;
    uint64_t total_files;
  };

  std::optional<EvaluateCleanupResult> evaluate_cleanup();

  std::vector<util::LockFile> acquire_all_level_2_content_locks(
    util::LongLivedLockFileManager& lock_manager, uint8_t l1_index);

  void clean_internal_tempdir();

  // Join the cache directory, a '/' and `name` into a single path and return
  // it. Additionally, `level` single-character, '/'-separated subpaths are
  // split from the beginning of `name` before joining them all.
  std::filesystem::path get_path_in_cache(uint8_t level,
                                          std::string_view name) const;

  std::filesystem::path get_lock_path(const std::string& name) const;

  util::LockFile get_auto_cleanup_lock() const;

  // A level 2 content lock grants exclusive access to a level 2 directory in
  // the cache. It must be acquired before adding, removing or recounting files
  // in the directory (including any subdirectories). However, the lock does not
  // have to be acquired to update a level 2 stats file since level 2 content
  // size and file count are stored in the parent (level 1) stats file.
  util::LockFile get_level_2_content_lock(const Hash::Digest& key) const;
  util::LockFile get_level_2_content_lock(uint8_t l1_index,
                                          uint8_t l2_index) const;
};

// --- Inline implementations ---

inline const core::StatisticsCounters&
LocalStorage::get_statistics_updates() const
{
  return m_counter_updates;
}

} // namespace storage::local
