// Copyright (C) 2002-2006 Andrew Tridgell
// Copyright (C) 2009-2021 Joel Rosdahl and other contributors
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

#include "PrimaryStorage.hpp"

#include <Config.hpp>
#include <Context.hpp>
#include <File.hpp>
#include <Logging.hpp>
#include <Util.hpp>
#include <core/CacheEntryReader.hpp>
#include <core/FileReader.hpp>
#include <fmtmacros.hpp>
#include <storage/primary/CacheFile.hpp>
#include <storage/primary/StatsFile.hpp>
#include <storage/primary/util.hpp>
#include <util/string.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include <InodeCache.hpp>
#endif

#include <algorithm>

using core::Statistic;

namespace storage {
namespace primary {

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
PrimaryStorage::evict(const ProgressReceiver& progress_receiver,
                      nonstd::optional<uint64_t> max_age,
                      nonstd::optional<std::string> namespace_)
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
PrimaryStorage::clean_dir(const std::string& subdir,
                          const uint64_t max_size,
                          const uint64_t max_files,
                          const nonstd::optional<uint64_t> max_age,
                          const nonstd::optional<std::string> namespace_,
                          const ProgressReceiver& progress_receiver)
{
  LOG("Cleaning up cache directory {}", subdir);

  std::vector<CacheFile> files = get_level_1_files(
    subdir, [&](double progress) { progress_receiver(progress / 3); });

  uint64_t cache_size = 0;
  uint64_t files_in_cache = 0;
  time_t current_time = time(nullptr);
  std::unordered_map<std::string /*result_file*/,
                     std::vector<std::string> /*associated_raw_files*/>
    raw_files_map;

  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(1.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file.lstat().is_regular()) {
      // Not a file or missing file.
      continue;
    }

    // Delete any tmp files older than 1 hour right away.
    if (file.lstat().mtime() + 3600 < current_time
        && Util::base_name(file.path()).find(".tmp.") != std::string::npos) {
      Util::unlink_tmp(file.path());
      continue;
    }

    if (namespace_ && file.type() == CacheFile::Type::raw) {
      const auto result_filename =
        FMT("{}R", file.path().substr(0, file.path().length() - 2));
      raw_files_map[result_filename].push_back(file.path());
    }

    cache_size += file.lstat().size_on_disk();
    files_in_cache += 1;
  }

  // Sort according to modification time, oldest first.
  std::sort(files.begin(), files.end(), [](const auto& f1, const auto& f2) {
    const auto ts_1 = f1.lstat().mtim();
    const auto ts_2 = f2.lstat().mtim();
    const auto ns_1 = 1'000'000'000ULL * ts_1.tv_sec + ts_1.tv_nsec;
    const auto ns_2 = 1'000'000'000ULL * ts_2.tv_sec + ts_2.tv_nsec;
    return ns_1 < ns_2;
  });

  LOG("Before cleanup: {:.0f} KiB, {:.0f} files",
      static_cast<double>(cache_size) / 1024,
      static_cast<double>(files_in_cache));

  bool cleaned = false;
  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(2.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file.lstat() || file.lstat().is_directory()) {
      continue;
    }

    if ((max_size == 0 || cache_size <= max_size)
        && (max_files == 0 || files_in_cache <= max_files)
        && (!max_age
            || file.lstat().mtime()
                 > (current_time - static_cast<int64_t>(*max_age)))
        && (!namespace_ || max_age)) {
      break;
    }

    if (namespace_) {
      try {
        File file_stream(file.path(), "rb");
        core::FileReader file_reader(*file_stream);
        core::CacheEntryReader reader(file_reader);
        if (reader.header().namespace_ != *namespace_) {
          continue;
        }
      } catch (core::Error&) {
        // Failed to read header: ignore.
        continue;
      }

      // For namespace eviction we need to remove raw files based on result
      // filename since they don't have a header.
      if (file.type() == CacheFile::Type::result) {
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

    if (util::ends_with(file.path(), ".stderr")) {
      // In order to be nice to legacy ccache versions, make sure that the .o
      // file is deleted before .stderr, because if the ccache process gets
      // killed after deleting the .stderr but before deleting the .o, the
      // cached result will be inconsistent. (.stderr is the only file that is
      // optional for legacy ccache versions; any other file missing from the
      // cache will be detected.)
      std::string o_file = file.path().substr(0, file.path().size() - 6) + "o";

      // Don't subtract this extra deletion from the cache size; that
      // bookkeeping will be done when the loop reaches the .o file. If the
      // loop doesn't reach the .o file since the target limits have been
      // reached, the bookkeeping won't happen, but that small counter
      // discrepancy won't do much harm and it will correct itself in the next
      // cleanup.
      delete_file(o_file, 0, nullptr, nullptr);
    }

    delete_file(
      file.path(), file.lstat().size_on_disk(), &cache_size, &files_in_cache);
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
PrimaryStorage::clean_all(const ProgressReceiver& progress_receiver)
{
  for_each_level_1_subdir(
    m_config.cache_dir(),
    [&](const std::string& subdir,
        const ProgressReceiver& sub_progress_receiver) {
      clean_dir(subdir,
                m_config.max_size() / 16,
                m_config.max_files() / 16,
                nonstd::nullopt,
                nonstd::nullopt,
                sub_progress_receiver);
    },
    progress_receiver);
}

// Wipe one cache subdirectory.
static void
wipe_dir(const std::string& subdir, const ProgressReceiver& progress_receiver)
{
  LOG("Clearing out cache directory {}", subdir);

  const std::vector<CacheFile> files = get_level_1_files(
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
PrimaryStorage::wipe_all(const ProgressReceiver& progress_receiver)
{
  for_each_level_1_subdir(m_config.cache_dir(), wipe_dir, progress_receiver);
}

} // namespace primary
} // namespace storage
