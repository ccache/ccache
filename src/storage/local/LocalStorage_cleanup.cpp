// Copyright (C) 2002-2006 Andrew Tridgell
// Copyright (C) 2009-2022 Joel Rosdahl and other contributors
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

#include <Config.hpp>
#include <Context.hpp>
#include <File.hpp>
#include <Logging.hpp>
#include <TemporaryFile.hpp>
#include <Util.hpp>
#include <core/CacheEntry.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <storage/local/StatsFile.hpp>
#include <storage/local/util.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include <InodeCache.hpp>
#endif

#include <algorithm>

using core::Statistic;

namespace storage::local {

static void
delete_file(const std::string& path,
            const uint64_t size,
            uint64_t* cache_size,
            uint64_t* files_in_cache)
{
  const bool deleted = Util::unlink_safe(path, Util::UnlinkLog::ignore_failure);
  if (!deleted && errno != ENOENT && errno != ESTALE) {
    LOG("Failed to unlink {} ({})", path, strerror(errno));
  } else if (cache_size && files_in_cache) {
    // The counters are intentionally subtracted even if there was no file to
    // delete since the final cache size calculation will be incorrect if they
    // aren't. (This can happen when there are several parallel ongoing
    // cleanups of the same directory.)
    *cache_size -= size;
    --*files_in_cache;
  }
}

static void
update_counters(const std::string& dir,
                const uint64_t files_in_cache,
                const uint64_t cache_size,
                const bool cleanup_performed)
{
  const std::string stats_file = dir + "/stats";
  StatsFile(stats_file).update([=](auto& cs) {
    if (cleanup_performed) {
      cs.increment(Statistic::cleanups_performed);
    }
    cs.set(Statistic::files_in_cache, files_in_cache);
    cs.set(Statistic::cache_size_kibibyte, cache_size / 1024);
  });
}

void
LocalStorage::evict(const ProgressReceiver& progress_receiver,
                    std::optional<uint64_t> max_age,
                    std::optional<std::string> namespace_)
{
  for_each_level_1_subdir(
    m_config.cache_dir(),
    [&](const std::string& subdir,
        const ProgressReceiver& sub_progress_receiver) {
      clean_dir(subdir, 0, 0, max_age, namespace_, sub_progress_receiver);
    },
    progress_receiver);
}

// Clean up one cache subdirectory.
void
LocalStorage::clean_dir(const std::string& subdir,
                        const uint64_t max_size,
                        const uint64_t max_files,
                        const std::optional<uint64_t> max_age,
                        const std::optional<std::string> namespace_,
                        const ProgressReceiver& progress_receiver)
{
  LOG("Cleaning up cache directory {}", subdir);

  auto files = get_level_1_files(
    subdir, [&](double progress) { progress_receiver(progress / 3); });

  uint64_t cache_size = 0;
  uint64_t files_in_cache = 0;
  auto current_time = util::TimePoint::now();
  std::unordered_map<std::string /*result_file*/,
                     std::vector<std::string> /*associated_raw_files*/>
    raw_files_map;

  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(1.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file.is_regular()) {
      // Not a file or missing file.
      continue;
    }

    // Delete any tmp files older than 1 hour right away.
    if (file.mtime() + util::Duration(3600) < current_time
        && TemporaryFile::is_tmp_file(file.path())) {
      Util::unlink_tmp(file.path());
      continue;
    }

    if (namespace_ && file_type_from_path(file.path()) == FileType::raw) {
      const auto result_filename =
        FMT("{}R", file.path().substr(0, file.path().length() - 2));
      raw_files_map[result_filename].push_back(file.path());
    }

    cache_size += file.size_on_disk();
    files_in_cache += 1;
  }

  // Sort according to modification time, oldest first.
  std::sort(files.begin(), files.end(), [](const auto& f1, const auto& f2) {
    return f1.mtime() < f2.mtime();
  });

  LOG("Before cleanup: {:.0f} KiB, {:.0f} files",
      static_cast<double>(cache_size) / 1024,
      static_cast<double>(files_in_cache));

  bool cleaned = false;
  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(2.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file || file.is_directory()) {
      continue;
    }

    if ((max_size == 0 || cache_size <= max_size)
        && (max_files == 0 || files_in_cache <= max_files)
        && (!max_age
            || file.mtime() > (current_time - util::Duration(*max_age)))
        && (!namespace_ || max_age)) {
      break;
    }

    if (namespace_) {
      try {
        core::CacheEntry::Header header(file.path());
        if (header.namespace_ != *namespace_) {
          continue;
        }
      } catch (core::Error&) {
        // Failed to read header: ignore.
        continue;
      }

      // For namespace eviction we need to remove raw files based on result
      // filename since they don't have a header.
      if (file_type_from_path(file.path()) == FileType::result) {
        const auto entry = raw_files_map.find(file.path());
        if (entry != raw_files_map.end()) {
          for (const auto& raw_file : entry->second) {
            delete_file(raw_file,
                        Stat::lstat(raw_file).size_on_disk(),
                        &cache_size,
                        &files_in_cache);
          }
        }
      }
    }

    delete_file(file.path(), file.size_on_disk(), &cache_size, &files_in_cache);
    cleaned = true;
  }

  LOG("After cleanup: {:.0f} KiB, {:.0f} files",
      static_cast<double>(cache_size) / 1024,
      static_cast<double>(files_in_cache));

  if (cleaned) {
    LOG("Cleaned up cache directory {}", subdir);
  }

  update_counters(subdir, files_in_cache, cache_size, cleaned);
}

// Clean up all cache subdirectories.
void
LocalStorage::clean_all(const ProgressReceiver& progress_receiver)
{
  for_each_level_1_subdir(
    m_config.cache_dir(),
    [&](const std::string& subdir,
        const ProgressReceiver& sub_progress_receiver) {
      clean_dir(subdir,
                m_config.max_size() / 16,
                m_config.max_files() / 16,
                std::nullopt,
                std::nullopt,
                sub_progress_receiver);
    },
    progress_receiver);
}

// Wipe one cache subdirectory.
static void
wipe_dir(const std::string& subdir, const ProgressReceiver& progress_receiver)
{
  LOG("Clearing out cache directory {}", subdir);

  const auto files = get_level_1_files(
    subdir, [&](double progress) { progress_receiver(progress / 2); });

  for (size_t i = 0; i < files.size(); ++i) {
    Util::unlink_safe(files[i].path());
    progress_receiver(0.5 + 0.5 * i / files.size());
  }

  const bool cleared = !files.empty();
  if (cleared) {
    LOG("Cleared out cache directory {}", subdir);
  }
  update_counters(subdir, 0, 0, cleared);
}

// Wipe all cached files in all subdirectories.
void
LocalStorage::wipe_all(const ProgressReceiver& progress_receiver)
{
  for_each_level_1_subdir(m_config.cache_dir(), wipe_dir, progress_receiver);
}

} // namespace storage::local
