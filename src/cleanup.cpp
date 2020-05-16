// Copyright (C) 2002-2006 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "cleanup.hpp"

#include "CacheFile.hpp"
#include "Config.hpp"
#include "Util.hpp"
#include "logging.hpp"
#include "stats.hpp"

#include <algorithm>
#include <math.h>

static void
delete_file(const std::string& path,
            uint64_t size,
            uint64_t* cache_size,
            uint32_t* files_in_cache)
{
  bool deleted = Util::unlink_safe(path, Util::UnlinkLog::ignore_failure);
  if (!deleted && errno != ENOENT && errno != ESTALE) {
    cc_log("Failed to unlink %s (%s)", path.c_str(), strerror(errno));
  } else if (cache_size && files_in_cache) {
    // The counters are intentionally subtracted even if there was no file to
    // delete since the final cache size calculation will be incorrect if they
    // aren't. (This can happen when there are several parallel ongoing
    // cleanups of the same directory.)
    *cache_size -= size;
    --*files_in_cache;
  }
}

// Clean up one cache subdirectory.
void
clean_up_dir(const std::string& subdir,
             uint64_t max_size,
             uint32_t max_files,
             const Util::ProgressReceiver& progress_receiver)
{
  cc_log("Cleaning up cache directory %s", subdir.c_str());

  std::vector<std::shared_ptr<CacheFile>> files;
  Util::get_level_1_files(
    subdir, [&](double progress) { progress_receiver(progress / 3); }, files);

  uint64_t cache_size = 0;
  uint32_t files_in_cache = 0;
  time_t current_time = time(nullptr);

  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(1.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file->lstat().is_regular()) {
      // Not a file or missing file.
      continue;
    }

    // Delete any tmp files older than 1 hour right away.
    if (file->lstat().mtime() + 3600 < current_time
        && Util::base_name(file->path()).find(".tmp.") != std::string::npos) {
      Util::unlink_tmp(file->path());
      continue;
    }

    cache_size += file->lstat().size_on_disk();
    files_in_cache += 1;
  }

  // Sort according to modification time, oldest first.
  std::sort(files.begin(),
            files.end(),
            [](const std::shared_ptr<CacheFile>& f1,
               const std::shared_ptr<CacheFile>& f2) {
              return f1->lstat().mtime() < f2->lstat().mtime();
            });

  cc_log("Before cleanup: %.0f KiB, %.0f files",
         static_cast<double>(cache_size) / 1024,
         static_cast<double>(files_in_cache));

  bool cleaned = false;
  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(2.0 / 3 + 1.0 * i / files.size() / 3)) {
    const auto& file = files[i];

    if (!file->lstat() || file->lstat().is_directory()) {
      continue;
    }

    if ((max_size == 0 || cache_size <= max_size)
        && (max_files == 0 || files_in_cache <= max_files)) {
      break;
    }

    if (Util::ends_with(file->path(), ".stderr")) {
      // In order to be nice to legacy ccache versions, make sure that the .o
      // file is deleted before .stderr, because if the ccache process gets
      // killed after deleting the .stderr but before deleting the .o, the
      // cached result will be inconsistent. (.stderr is the only file that is
      // optional for legacy ccache versions; any other file missing from the
      // cache will be detected.)
      std::string o_file =
        file->path().substr(0, file->path().size() - 6) + "o";

      // Don't subtract this extra deletion from the cache size; that
      // bookkeeping will be done when the loop reaches the .o file. If the
      // loop doesn't reach the .o file since the target limits have been
      // reached, the bookkeeping won't happen, but that small counter
      // discrepancy won't do much harm and it will correct itself in the next
      // cleanup.
      delete_file(o_file, 0, nullptr, nullptr);
    }

    delete_file(
      file->path(), file->lstat().size_on_disk(), &cache_size, &files_in_cache);
    cleaned = true;
  }

  cc_log("After cleanup: %.0f KiB, %.0f files",
         static_cast<double>(cache_size) / 1024,
         static_cast<double>(files_in_cache));

  if (cleaned) {
    cc_log("Cleaned up cache directory %s", subdir.c_str());
    stats_add_cleanup(subdir.c_str(), 1);
  }

  stats_set_sizes(subdir.c_str(), files_in_cache, cache_size);
}

// Clean up all cache subdirectories.
void
clean_up_all(const Config& config,
             const Util::ProgressReceiver& progress_receiver)
{
  Util::for_each_level_1_subdir(
    config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      clean_up_dir(subdir,
                   config.max_size() / 16,
                   config.max_files() / 16,
                   sub_progress_receiver);
    },
    progress_receiver);
}

// Wipe one cache subdirectory.
static void
wipe_dir(const std::string& subdir,
         const Util::ProgressReceiver& progress_receiver)
{
  cc_log("Clearing out cache directory %s", subdir.c_str());

  std::vector<std::shared_ptr<CacheFile>> files;
  Util::get_level_1_files(
    subdir, [&](double progress) { progress_receiver(progress / 2); }, files);

  for (size_t i = 0; i < files.size(); ++i) {
    Util::unlink_safe(files[i]->path());
    progress_receiver(0.5 + 0.5 * i / files.size());
  }

  if (!files.empty()) {
    cc_log("Cleared out cache directory %s", subdir.c_str());
    stats_add_cleanup(subdir.c_str(), 1);
  }

  stats_set_sizes(subdir.c_str(), 0, 0);
}

// Wipe all cached files in all subdirectories.
void
wipe_all(const Config& config, const Util::ProgressReceiver& progress_receiver)
{
  Util::for_each_level_1_subdir(
    config.cache_dir(), wipe_dir, progress_receiver);
}
