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

#include "localstorage.hpp"

#include <ccache/config.hpp>
#include <ccache/context.hpp>
#include <ccache/core/atomicfile.hpp>
#include <ccache/core/cacheentry.hpp>
#include <ccache/core/common.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/core/filerecompressor.hpp>
#include <ccache/core/manifest.hpp>
#include <ccache/core/statistics.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filestream.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/pathstring.hpp>
#include <ccache/util/process.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/temporaryfile.hpp>
#include <ccache/util/texttable.hpp>
#include <ccache/util/threadpool.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include <ccache/inodecache.hpp>
#endif

#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef __linux__
#  ifdef HAVE_SYS_IOCTL_H
#    include <sys/ioctl.h>
#  endif
#  ifdef HAVE_LINUX_FS_H
#    include <linux/fs.h>
#    ifndef FICLONE
#      define FICLONE _IOW(0x94, 9, int)
#    endif
#    define FILE_CLONING_SUPPORTED 1
#  endif
#endif

#ifdef __APPLE__
#  ifdef HAVE_SYS_CLONEFILE_H
#    include <sys/clonefile.h>
#    ifdef CLONE_NOOWNERCOPY
#      define FILE_CLONING_SUPPORTED 1
#    endif
#  endif
#endif

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <string>
#include <utility>

namespace fs = util::filesystem;

using namespace std::literals::chrono_literals;

using core::AtomicFile;
using core::Statistic;
using core::StatisticsCounters;
using util::DirEntry;

namespace storage::local {

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
const auto k_tempdir_cleanup_interval = 2 * 24 * 60 * 60s; // C++20: 2d

// Maximum files per cache directory. This constant is somewhat arbitrarily
// chosen to be large enough to avoid unnecessary cache levels but small enough
// not to make it too slow for legacy file systems with bad performance for
// large directories. It could be made configurable, but hopefully there will be
// no need to do that.
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

namespace {

struct Level2Counters
{
  uint64_t files = 0;
  uint64_t size = 0;

  Level2Counters&
  operator+=(const Level2Counters& other)
  {
    files += other.files;
    size += other.size;
    return *this;
  }
};

struct Level1Counters
{
  Level2Counters level_2_counters[16] = {};
  uint64_t cleanups = 0;

  uint64_t
  files() const
  {
    uint64_t sum = 0;
    for (const auto& cs : level_2_counters) {
      sum += cs.files;
    }
    return sum;
  }

  uint64_t
  size() const
  {
    uint64_t sum = 0;
    for (const auto& cs : level_2_counters) {
      sum += cs.size;
    }
    return sum;
  }
};

} // namespace

// Return size change in KiB between `old_dir_entry` and `new_dir_entry`.
static int64_t
kibibyte_size_diff(const DirEntry& old_dir_entry, const DirEntry& new_dir_entry)
{
  return (static_cast<int64_t>(new_dir_entry.size_on_disk())
          - static_cast<int64_t>(old_dir_entry.size_on_disk()))
         / 1024;
}

static void
set_counters(const StatsFile& stats_file, const Level1Counters& level_1_cs)
{
  stats_file.update([&](auto& cs) {
    cs.set(Statistic::files_in_cache, level_1_cs.files());
    cs.set(Statistic::cache_size_kibibyte, level_1_cs.size() / 1024);
    for_each_cache_subdir([&](uint8_t i) {
      cs.set_offsetted(
        Statistic::subdir_files_base, i, level_1_cs.level_2_counters[i].files);
      cs.set_offsetted(Statistic::subdir_size_kibibyte_base,
                       i,
                       level_1_cs.level_2_counters[i].size / 1024);
    });
    cs.increment(Statistic::cleanups_performed, level_1_cs.cleanups);
  });
}

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

static void
delete_file(const DirEntry& dir_entry,
            uint64_t& cache_size,
            uint64_t& files_in_cache)
{
  const auto result =
    util::remove_nfs_safe(dir_entry.path(), util::LogFailure::no);
  if (!result && result.error().value() != ENOENT
      && result.error().value() != ESTALE) {
    LOG("Failed to unlink {} ({})", dir_entry.path(), strerror(errno));
  } else {
    // The counters are intentionally subtracted even if there was no file to
    // delete since the final cache size calculation will be incorrect if they
    // aren't. (This can happen when there are several parallel ongoing
    // cleanups of the same directory.)
    cache_size -= dir_entry.size_on_disk();
    --files_in_cache;
  }
}

#ifdef FILE_CLONING_SUPPORTED

// Clone a file from `src` to `dest`. If `via_tmp_file` is true, `src` is cloned
// to a temporary file and then renamed to `dest`. Throws `core::Error` on
// error.
static void
clone_file(const fs::path& src, const fs::path& dest, bool via_tmp_file)
{
#  if defined(__linux__)
  util::Fd src_fd(open(util::pstr(src).c_str(), O_RDONLY));
  if (!src_fd) {
    throw core::Error(FMT("{}: {}", src, strerror(errno)));
  }

  util::Fd dest_fd;
  std::string tmp_file;
  if (via_tmp_file) {
    auto temp_file =
      util::value_or_throw<core::Fatal>(util::TemporaryFile::create(dest));
    dest_fd = std::move(temp_file.fd);
    tmp_file = std::move(temp_file.path);
  } else {
    dest_fd = util::Fd(
      open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      throw core::Error(FMT("{}: {}", src, strerror(errno)));
    }
  }

  if (ioctl(*dest_fd, FICLONE, *src_fd) != 0) {
    throw core::Error(strerror(errno));
  }

  dest_fd.close();
  src_fd.close();

  if (via_tmp_file) {
    const auto result = fs::rename(tmp_file, dest);
    if (!result) {
      throw core::Error(FMT("failed to rename {} to {}: {}",
                            tmp_file,
                            dest,
                            result.error().message()));
    }
  }
#  elif defined(__APPLE__)
  (void)via_tmp_file;
  if (clonefile(
        util::pstr(src).c_str(), util::pstr(dest).c_str(), CLONE_NOOWNERCOPY)
      != 0) {
    throw core::Error(strerror(errno));
  }
#  else
  (void)src;
  (void)dest;
  (void)via_tmp_file;
  throw core::Error(strerror(EOPNOTSUPP));
#  endif
}

#endif // FILE_CLONING_SUPPORTED

struct CleanDirResult
{
  Level2Counters before;
  Level2Counters after;
};

template<typename T>
static double
ratio(T numerator, T denominator)
{
  if (denominator == 0) {
    return 0.0;
  } else {
    return static_cast<double>(numerator) / static_cast<double>(denominator);
  }
}

static CleanDirResult
clean_dir(
  const fs::path& l2_dir,
  const uint64_t max_size,
  const uint64_t max_files,
  const std::optional<uint64_t> max_age = std::nullopt,
  const std::optional<std::string> namespace_ = std::nullopt,
  const ProgressReceiver& progress_receiver = [](double /*progress*/) {})
{
  LOG("Cleaning up cache directory {}", l2_dir);

  auto files = get_cache_dir_files(l2_dir);
  progress_receiver(1.0 / 3);

  uint64_t cache_size = 0;
  uint64_t files_in_cache = 0;
  auto current_time = util::now();
  std::unordered_map<std::string /*result_file*/,
                     std::vector<fs::path> /*associated_raw_files*/>
    raw_files_map;

  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(1.0 / 3 + 1.0 * ratio(i, files.size()) / 3)) {
    const auto& file = files[i];

    if (!file.is_regular_file()) {
      // Not a file or missing file.
      continue;
    }

    // Delete any tmp files older than 1 hour right away.
    if (file.mtime() + 1h < current_time
        && util::TemporaryFile::is_tmp_file(file.path())) {
      std::ignore = util::remove(file.path());
      continue;
    }

    if (namespace_ && file_type_from_path(file.path()) == FileType::raw) {
      util::PathString path_str(file.path());
      const std::string result_path =
        FMT("{}R", path_str.str().substr(0, path_str.str().length() - 2));
      raw_files_map[result_path].push_back(file.path());
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
  Level2Counters counters_before{files_in_cache, cache_size};

  bool cleaned = false;
  for (size_t i = 0; i < files.size();
       ++i, progress_receiver(2.0 / 3 + 1.0 * ratio(i, files.size()) / 3)) {
    const auto& file = files[i];

    if (!file || file.is_directory()) {
      continue;
    }

    if ((max_size == 0 || cache_size <= max_size)
        && (max_files == 0 || files_in_cache <= max_files)
        && (!max_age
            || file.mtime() > (current_time - std::chrono::seconds(*max_age)))
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
        const auto entry = raw_files_map.find(util::pstr(file.path()));
        if (entry != raw_files_map.end()) {
          for (const auto& raw_file : entry->second) {
            delete_file(DirEntry(raw_file), cache_size, files_in_cache);
          }
        }
      }
    }

    delete_file(file, cache_size, files_in_cache);
    cleaned = true;
  }

  LOG("After cleanup: {:.0f} KiB, {:.0f} files",
      static_cast<double>(cache_size) / 1024,
      static_cast<double>(files_in_cache));
  Level2Counters counters_after{files_in_cache, cache_size};

  if (cleaned) {
    LOG("Cleaned up cache directory {}", l2_dir);
  }

  return {counters_before, counters_after};
}

FileType
file_type_from_path(const fs::path& path)
{
  std::string filename = path.filename().string();
  if (util::ends_with(filename, "M")) {
    return FileType::manifest;
  } else if (util::ends_with(filename, "R")) {
    return FileType::result;
  } else if (util::ends_with(filename, "W")) {
    return FileType::raw;
  } else {
    return FileType::unknown;
  }
}

LocalStorage::LocalStorage(const Config& config)
  : m_config(config)
{
}

void
LocalStorage::finalize()
{
  if (m_config.stats() && !m_counter_updates.all_zero()) {
    // Pseudo-randomly choose one of the stats files in the 256 level 2
    // directories.
    const auto bucket = getpid() % 256;
    const uint8_t l1_index = static_cast<uint8_t>(bucket / 16);
    const uint8_t l2_index = static_cast<uint8_t>(bucket % 16);
    const auto l2_stats_file = get_stats_file(l1_index, l2_index);

    uint64_t l2_files_in_cache = 0;
    uint64_t l2_cache_size_kibibyte = 0;

    l2_stats_file.update([&](auto& cs) {
      cs.increment(m_counter_updates);

      if (m_stored_data) {
        // Ccache 4.8-4.8.2 erroneously stored files/size counters for raw files
        // in L2, so move them to L1 to make the cleanup algorithm aware.
        l2_files_in_cache = cs.get(Statistic::files_in_cache);
        l2_cache_size_kibibyte = cs.get(Statistic::cache_size_kibibyte);
        cs.set(Statistic::files_in_cache, 0);
        cs.set(Statistic::cache_size_kibibyte, 0);
      }
    });

    if (m_stored_data) {
      // See comment about ccache 4.8-4.8.2 above.
      if (l2_files_in_cache > 0 || l2_cache_size_kibibyte > 0) {
        increment_files_and_size_counters(
          l1_index, l2_index, l2_files_in_cache, l2_cache_size_kibibyte);
      }

      perform_automatic_cleanup();
    }
  }

  if (m_config.temporary_dir() == m_config.default_temporary_dir()) {
    clean_internal_tempdir();
  }
}

std::optional<util::Bytes>
LocalStorage::get(const Hash::Digest& key, const core::CacheEntryType type)
{
  std::optional<util::Bytes> return_value;

  const auto cache_file = look_up_cache_file(key, type);
  if (cache_file.dir_entry.is_regular_file()) {
    const auto value = util::read_file<util::Bytes>(cache_file.path);
    if (value) {
      LOG("Retrieved {} from local storage ({})",
          util::format_digest(key),
          cache_file.path);

      // Update modification timestamp to save file from LRU cleanup.
      util::set_timestamps(cache_file.path);

      return_value = *value;
    } else {
      LOG("Failed to read {}: {}", cache_file.path, value.error());
    }
  } else {
    LOG("No {} in local storage", util::format_digest(key));
  }

  increment_statistic(return_value ? Statistic::local_storage_read_hit
                                   : Statistic::local_storage_read_miss);
  if (return_value && type == core::CacheEntryType::result) {
    increment_statistic(Statistic::local_storage_hit);
  }

  return return_value;
}

void
LocalStorage::put(const Hash::Digest& key,
                  const core::CacheEntryType type,
                  nonstd::span<const uint8_t> value,
                  Overwrite overwrite)
{
  const auto cache_file = look_up_cache_file(key, type);
  if (overwrite == Overwrite::no && cache_file.dir_entry.exists()) {
    LOG("Not storing {} in local storage since it already exists",
        cache_file.path);
    return;
  }

  auto l2_content_lock = get_level_2_content_lock(key);

  try {
    AtomicFile result_file(cache_file.path, AtomicFile::Mode::binary);
    result_file.write(value);
    result_file.flush();
    if (!l2_content_lock.acquire()) {
      LOG("Not storing {} due to lock failure", cache_file.path);
      return;
    }
    result_file.commit();
  } catch (core::Error& e) {
    LOG("Failed to write to {}: {}", cache_file.path, e.what());
    return;
  }

  LOG("Stored {} in local storage ({})",
      util::format_digest(key),
      cache_file.path);
  m_stored_data = true;

  if (!m_config.stats()) {
    return;
  }

  increment_statistic(Statistic::local_storage_write);

  DirEntry new_dir_entry(cache_file.path, DirEntry::LogOnError::yes);
  if (!new_dir_entry.exists()) {
    return;
  }

  int64_t files_change = cache_file.dir_entry.exists() ? 0 : 1;
  int64_t size_change_kibibyte =
    kibibyte_size_diff(cache_file.dir_entry, new_dir_entry);
  auto counters =
    increment_files_and_size_counters(key, files_change, size_change_kibibyte);

  l2_content_lock.release();

  if (!counters) {
    return;
  }

  move_to_wanted_cache_level(*counters, key, type, cache_file.path);

  // Make sure we have a CACHEDIR.TAG in the cache part of cache_dir. This can
  // be done almost anywhere, but we might as well do it near the end as we save
  // the stat call if we exit early.
  util::create_cachedir_tag(
    FMT("{}/{}", m_config.cache_dir(), util::format_digest(key)[0]));
}

void
LocalStorage::remove(const Hash::Digest& key, const core::CacheEntryType type)
{
  const auto cache_file = look_up_cache_file(key, type);
  if (!cache_file.dir_entry) {
    LOG("No {} to remove from local storage", util::format_digest(key));
    return;
  }

  increment_statistic(Statistic::local_storage_write);

  {
    auto l2_content_lock = get_level_2_content_lock(key);
    if (!l2_content_lock.acquire()) {
      LOG("Not removing {} due to lock failure", cache_file.path);
    }
    std::ignore = util::remove_nfs_safe(cache_file.path);
  }

  LOG("Removed {} from local storage ({})",
      util::format_digest(key),
      cache_file.path);
  increment_files_and_size_counters(
    key, -1, -static_cast<int64_t>(cache_file.dir_entry.size_on_disk() / 1024));
}

fs::path
LocalStorage::get_raw_file_path(const fs::path& result_path,
                                uint8_t file_number)
{
  if (file_number >= 10) {
    // To support more entries in the future, encode to [0-9a-z]. Note that
    // LocalStorage::evict currently assumes that the entry number is
    // represented as one character.
    throw core::Error(FMT("Too high raw file entry number: {}", file_number));
  }

  std::string s = result_path.string();
  s.pop_back();
  return FMT("{}{}W", s, file_number);
}

fs::path
LocalStorage::get_raw_file_path(const Hash::Digest& result_key,
                                uint8_t file_number) const
{
  const auto cache_file =
    look_up_cache_file(result_key, core::CacheEntryType::result);
  return get_raw_file_path(cache_file.path, file_number);
}

void
LocalStorage::put_raw_files(
  const Hash::Digest& key,
  const std::vector<core::result::Serializer::RawFile>& raw_files)
{
  const auto cache_file = look_up_cache_file(key, core::CacheEntryType::result);
  core::ensure_dir_exists(cache_file.path.parent_path());

  int64_t files_change = 0;
  int64_t size_kibibyte_change = 0;

  for (auto [file_number, source_path] : raw_files) {
    const auto dest_path = get_raw_file_path(cache_file.path, file_number);
    DirEntry old_dir_entry(dest_path);
    old_dir_entry.refresh();
    try {
      clone_hard_link_or_copy_file(source_path, dest_path, true);
      m_added_raw_files.push_back(AddedRawFile{file_number, dest_path});
    } catch (core::Error& e) {
      LOG("Failed to store {} as raw file {}: {}",
          source_path,
          dest_path,
          e.what());
      throw;
    }
    DirEntry new_dir_entry(dest_path);
    files_change += (new_dir_entry ? 1 : 0) - (old_dir_entry ? 1 : 0);
    size_kibibyte_change += kibibyte_size_diff(old_dir_entry, new_dir_entry);
  }

  increment_files_and_size_counters(key, files_change, size_kibibyte_change);
}

void
LocalStorage::clone_hard_link_or_copy_file(const fs::path& source,
                                           const fs::path& dest,
                                           bool via_tmp_file) const
{
  if (m_config.file_clone()) {
#ifdef FILE_CLONING_SUPPORTED
    LOG("Cloning {} to {}", source, dest);
    try {
      clone_file(source, dest, via_tmp_file);
      return;
    } catch (core::Error& e) {
      LOG("Failed to clone: {}", e.what());
    }
#else
    LOG("Not cloning {} to {} since it's unsupported", source, dest);
#endif
  }
  if (m_config.hard_link()) {
    // Assumption: dest may already exist as a left-over file from a previous
    // run, but it's only we who can create the file entry now so we don't try
    // to handle a race between remove() and create_hard_link() below.

    std::ignore = fs::remove(dest); // Ignore any error.
    LOG("Hard linking {} to {}", source, dest);
    if (auto result = fs::create_hard_link(source, dest); !result) {
      LOG("Failed to hard link {} to {}: {}",
          source,
          dest,
          result.error().message());
      // Fall back to copying.
    } else {
      // Success.
#ifndef _WIN32
      if (chmod(dest.c_str(), 0444 & ~util::get_umask()) != 0) {
        LOG("Failed to chmod {}: {}", dest.c_str(), strerror(errno));
      }
#endif
      return;
    }
  }

  LOG("Copying {} to {}", source, dest);
  util::throw_on_error<core::Error>(
    util::copy_file(source,
                    dest,
                    via_tmp_file ? util::ViaTmpFile::yes
                                 : util::ViaTmpFile::no),
    FMT("Failed to copy {} to {}: ", source, dest));
}

void
LocalStorage::increment_statistic(const Statistic statistic,
                                  const int64_t value)
{
  if (m_config.stats()) {
    m_counter_updates.increment(statistic, value);
  }
}

void
LocalStorage::increment_statistics(const StatisticsCounters& statistics)
{
  if (m_config.stats()) {
    m_counter_updates.increment(statistics);
  }
}

// Zero all statistics counters except those tracking cache size and number of
// files in the cache.
void
LocalStorage::zero_all_statistics()
{
  const auto now = util::now();
  const auto zeroable_fields = core::Statistics::get_zeroable_fields();

  for_each_level_1_and_2_stats_file(
    m_config.cache_dir(), [=](const fs::path& path) {
      StatsFile(path).update([=](auto& cs) {
        for (const auto statistic : zeroable_fields) {
          cs.set(statistic, 0);
        }
        cs.set(Statistic::stats_zeroed_timestamp, util::sec(now));
      });
    });
}

// Get statistics and last time of update for the whole local storage cache.
std::pair<StatisticsCounters, util::TimePoint>
LocalStorage::get_all_statistics() const
{
  StatisticsCounters counters;
  uint64_t zero_timestamp = 0;
  util::TimePoint last_updated;

  // Add up the stats in each directory.
  for_each_level_1_and_2_stats_file(
    m_config.cache_dir(), [&](const auto& path) {
      counters.set(Statistic::stats_zeroed_timestamp, 0); // Don't add
      counters.increment(StatsFile(path).read());
      zero_timestamp = std::max(counters.get(Statistic::stats_zeroed_timestamp),
                                zero_timestamp);
      last_updated = std::max(last_updated, DirEntry(path).mtime());
    });

  counters.set(Statistic::stats_zeroed_timestamp, zero_timestamp);
  return {counters, last_updated};
}

void
LocalStorage::evict(const ProgressReceiver& progress_receiver,
                    std::optional<uint64_t> max_age,
                    std::optional<std::string> namespace_)
{
  do_clean_all(progress_receiver, 0, 0, max_age, namespace_);
}

void
LocalStorage::clean_all(const ProgressReceiver& progress_receiver)
{
  do_clean_all(progress_receiver,
               m_config.max_size(),
               m_config.max_files(),
               std::nullopt,
               std::nullopt);
}

// Wipe all cached files in all subdirectories.
void
LocalStorage::wipe_all(const ProgressReceiver& progress_receiver)
{
  util::LongLivedLockFileManager lock_manager;

  for_each_cache_subdir(
    progress_receiver, [&](uint8_t l1_index, const auto& l1_progress_receiver) {
      auto acquired_locks =
        acquire_all_level_2_content_locks(lock_manager, l1_index);
      Level1Counters level_1_counters;

      for_each_cache_subdir(
        l1_progress_receiver,
        [&](uint8_t l2_index, const ProgressReceiver& l2_progress_receiver) {
          auto l2_dir = get_subdir(l1_index, l2_index);
          const auto files = get_cache_dir_files(l2_dir);
          l2_progress_receiver(0.5);

          for (size_t i = 0; i < files.size(); ++i) {
            std::ignore = util::remove_nfs_safe(files[i].path());
            l2_progress_receiver(0.5 + 0.5 * ratio(i, files.size()));
          }

          if (!files.empty()) {
            ++level_1_counters.cleanups;
          }
        });

      set_counters(get_stats_file(l1_index), level_1_counters);
    });
}

CompressionStatistics
LocalStorage::get_compression_statistics(
  const ProgressReceiver& progress_receiver) const
{
  CompressionStatistics cs{};

  for_each_cache_subdir(
    progress_receiver,
    [&](const auto& l1_index, const auto& l1_progress_receiver) {
      for_each_cache_subdir(
        l1_progress_receiver,
        [&](const auto& l2_index, const auto& l2_progress_receiver) {
          auto l2_dir = get_subdir(l1_index, l2_index);
          const auto files = get_cache_dir_files(l2_dir);
          l2_progress_receiver(0.2);

          for (size_t i = 0; i < files.size(); ++i) {
            const auto& cache_file = files[i];
            try {
              core::CacheEntry::Header header(cache_file.path());
              cs.actual_size += cache_file.size_on_disk();
              cs.content_size += util::likely_size_on_disk(header.entry_size);
            } catch (core::Error&) {
              cs.incompressible_size += cache_file.size_on_disk();
            }
            l2_progress_receiver(0.2 + 0.8 * ratio(i, files.size()));
          }
        });
    });

  return cs;
}

void
LocalStorage::recompress(const std::optional<int8_t> level,
                         const uint32_t threads,
                         const ProgressReceiver& progress_receiver)
{
  const size_t read_ahead =
    std::max(static_cast<size_t>(10), 2 * static_cast<size_t>(threads));
  util::ThreadPool thread_pool(threads, read_ahead);
  core::FileRecompressor recompressor;

  std::atomic<uint64_t> incompressible_size = 0;
  util::LongLivedLockFileManager lock_manager;

  for_each_cache_subdir(
    progress_receiver,
    [&](const auto& l1_index, const auto& l1_progress_receiver) {
      for_each_cache_subdir(
        l1_progress_receiver,
        [&](const auto& l2_index, const auto& l2_progress_receiver) {
          auto l2_content_lock = get_level_2_content_lock(l1_index, l2_index);
          l2_content_lock.make_long_lived(lock_manager);
          if (!l2_content_lock.acquire()) {
            // LOG_RAW+fmt::format instead of LOG due to GCC 12.3 bug #109241
            LOG_RAW(fmt::format(
              "Failed to acquire content lock for {}/{}", l1_index, l2_index));
            return;
          }

          auto l2_dir = get_subdir(l1_index, l2_index);
          auto files = get_cache_dir_files(l2_dir);
          l2_progress_receiver(0.1);

          auto stats_file = get_stats_file(l1_index);

          for (size_t i = 0; i < files.size(); ++i) {
            const auto& file = files[i];

            if (file_type_from_path(file.path()) != FileType::unknown) {
              thread_pool.enqueue([=, &recompressor, &incompressible_size] {
                try {
                  DirEntry new_dir_entry = recompressor.recompress(
                    file, level, core::FileRecompressor::KeepAtime::no);
                  auto size_change_kibibyte =
                    kibibyte_size_diff(file, new_dir_entry);
                  if (size_change_kibibyte != 0) {
                    StatsFile(stats_file).update([=](auto& cs) {
                      cs.increment(Statistic::cache_size_kibibyte,
                                   size_change_kibibyte);
                      cs.increment_offsetted(
                        Statistic::subdir_size_kibibyte_base,
                        l2_index,
                        size_change_kibibyte);
                    });
                  }
                } catch (core::Error&) {
                  // Ignore for now.
                  incompressible_size += file.size_on_disk();
                }
              });
            } else if (!util::TemporaryFile::is_tmp_file(file.path())) {
              incompressible_size += file.size_on_disk();
            }

            l2_progress_receiver(0.1 + 0.9 * ratio(i, files.size()));
          }

          if (l2_dir.filename() == "f"
              && l2_dir.parent_path().filename() == "f") {
            // Wait here instead of after for_each_cache_subdir to avoid
            // updating the progress bar to 100% before all work is done.
            thread_pool.shut_down();
          }
        });
    });

  // In case there was no f/f subdir, shut down the thread pool now.
  thread_pool.shut_down();

  if (isatty(STDOUT_FILENO)) {
    PRINT_RAW(stdout, "\n\n");
  }

  const double old_ratio =
    ratio(recompressor.content_size(), recompressor.old_size());
  const double old_savings =
    old_ratio > 0.0 ? 100.0 - (100.0 / old_ratio) : 0.0;
  const double new_ratio =
    ratio(recompressor.content_size(), recompressor.new_size());
  const double new_savings =
    new_ratio > 0.0 ? 100.0 - (100.0 / new_ratio) : 0.0;
  const int64_t size_diff = static_cast<int64_t>(recompressor.new_size())
                            - static_cast<int64_t>(recompressor.old_size());

  auto human_readable = [&](uint64_t size) {
    return util::format_human_readable_size(size,
                                            m_config.size_unit_prefix_type());
  };

  const auto [old_compr_size_quantity, old_compr_size_unit] =
    util::split_once(human_readable(recompressor.old_size()), ' ');
  ASSERT(old_compr_size_unit);
  const auto [new_compr_size_quantity, new_compr_size_unit] =
    util::split_once(human_readable(recompressor.new_size()), ' ');
  ASSERT(new_compr_size_unit);
  const auto [content_size_quantity, content_size_unit] =
    util::split_once(human_readable(recompressor.content_size()), ' ');
  ASSERT(content_size_unit);
  const auto [size_diff_quantity, size_diff_unit] =
    util::split_once(human_readable(std::abs(size_diff)), ' ');
  ASSERT(size_diff_unit);

  using C = util::TextTable::Cell;
  util::TextTable table;

  table.add_row({
    "Original data:",
    C(content_size_quantity).right_align(),
    *content_size_unit,
  });
  table.add_row({
    "Old compressed data:",
    C(old_compr_size_quantity).right_align(),
    *old_compr_size_unit,
    FMT("({:.1f}% of original size)", 100.0 - old_savings),
  });
  table.add_row({
    "  Compression ratio:",
    C(FMT("{:5.3f}", old_ratio)).right_align(),
    "x",
    FMT("({:.1f}% space savings)", old_savings),
  });
  table.add_row({
    "New compressed data:",
    C(new_compr_size_quantity).right_align(),
    *new_compr_size_unit,
    FMT("({:.1f}% of original size)", 100.0 - new_savings),
  });
  table.add_row({
    "  Compression ratio:",
    C(FMT("{:5.3f}", new_ratio)).right_align(),
    "x",
    FMT("({:.1f}% space savings)", new_savings),
  });
  table.add_row({
    "Size change:",
    C(FMT("{}{}", size_diff < 0 ? "-" : "", size_diff_quantity)).right_align(),
    *size_diff_unit,
  });

  PRINT_RAW(stdout, table.render());
}

// Private methods

fs::path
LocalStorage::get_subdir(uint8_t l1_index) const
{
  return m_config.cache_dir() / FMT("{:x}", l1_index);
}

fs::path
LocalStorage::get_subdir(uint8_t l1_index, uint8_t l2_index) const
{
  return m_config.cache_dir() / FMT("{:x}/{:x}", l1_index, l2_index);
}

LocalStorage::LookUpCacheFileResult
LocalStorage::look_up_cache_file(const Hash::Digest& key,
                                 const core::CacheEntryType type) const
{
  const auto key_string =
    FMT("{}{}", util::format_digest(key), suffix_from_type(type));

  for (uint8_t level = k_min_cache_levels; level <= k_max_cache_levels;
       ++level) {
    const auto path = get_path_in_cache(level, key_string);
    DirEntry dir_entry(path);
    if (dir_entry.is_regular_file()) {
      return {path, dir_entry, level};
    }
  }

  const auto shallowest_path =
    get_path_in_cache(k_min_cache_levels, key_string);
  return {shallowest_path, DirEntry(), k_min_cache_levels};
}

StatsFile
LocalStorage::get_stats_file(uint8_t l1_index) const
{
  return StatsFile(FMT("{}/{:x}/stats", m_config.cache_dir(), l1_index));
}

StatsFile
LocalStorage::get_stats_file(uint8_t l1_index, uint8_t l2_index) const
{
  return StatsFile(
    FMT("{}/{:x}/{:x}/stats", m_config.cache_dir(), l1_index, l2_index));
}

void
LocalStorage::move_to_wanted_cache_level(const StatisticsCounters& counters,
                                         const Hash::Digest& key,
                                         core::CacheEntryType type,
                                         const fs::path& cache_file_path)
{
  const auto wanted_level =
    calculate_wanted_cache_level(counters.get(Statistic::files_in_cache));
  const auto wanted_path = get_path_in_cache(
    wanted_level, util::format_digest(key) + suffix_from_type(type));
  if (cache_file_path != wanted_path) {
    core::ensure_dir_exists(wanted_path.parent_path());

    // Note: Two ccache processes may move the file at the same time, so failure
    // to rename is OK.
    LOG("Moving {} to {}", cache_file_path, wanted_path);
    std::ignore = fs::rename(cache_file_path, wanted_path);
    for (const auto& [file_number, dest_path] : m_added_raw_files) {
      std::ignore =
        fs::rename(dest_path, get_raw_file_path(wanted_path, file_number));
    }
  }
}

void
LocalStorage::recount_level_1_dir(util::LongLivedLockFileManager& lock_manager,
                                  uint8_t l1_index)
{
  auto acquired_locks =
    acquire_all_level_2_content_locks(lock_manager, l1_index);
  Level1Counters level_1_counters;

  for_each_cache_subdir([&](uint8_t l2_index) {
    auto files = get_cache_dir_files(get_subdir(l1_index, l2_index));
    auto& level_2_counters = level_1_counters.level_2_counters[l2_index];
    level_2_counters.files = files.size();
    for (const auto& file : files) {
      level_2_counters.size += file.size_on_disk();
    }
  });

  set_counters(get_stats_file(l1_index), level_1_counters);
}

std::optional<core::StatisticsCounters>
LocalStorage::increment_files_and_size_counters(uint8_t l1_index,
                                                uint8_t l2_index,
                                                int64_t files,
                                                int64_t size_kibibyte)
{
  const auto level_1_stats_file = get_stats_file(l1_index);
  return level_1_stats_file.update([&](auto& cs) {
    // Level 1 counters:
    cs.increment(Statistic::files_in_cache, files);
    cs.increment(Statistic::cache_size_kibibyte, size_kibibyte);

    // Level 2 counters:
    cs.increment_offsetted(Statistic::subdir_files_base, l2_index, files);
    cs.increment_offsetted(
      Statistic::subdir_size_kibibyte_base, l2_index, size_kibibyte);
  });
}

std::optional<core::StatisticsCounters>
LocalStorage::increment_files_and_size_counters(const Hash::Digest& key,
                                                int64_t files,
                                                int64_t size_kibibyte)
{
  return increment_files_and_size_counters(
    key[0] >> 4, key[0] & 0xF, files, size_kibibyte);
}

static uint8_t
get_largest_level_2_index(const StatisticsCounters& counters)
{
  uint64_t largest_level_2_files = 0;
  uint8_t largest_level_2_index = 0;
  for_each_cache_subdir([&](uint8_t i) {
    uint64_t l2_files =
      1024 * counters.get_offsetted(Statistic::subdir_files_base, i);
    if (l2_files > largest_level_2_files) {
      largest_level_2_files = l2_files;
      largest_level_2_index = i;
    }
  });
  return largest_level_2_index;
}

static bool
has_consistent_counters(const StatisticsCounters& counters)
{
  uint64_t level_2_files = 0;
  uint64_t level_2_size_kibibyte = 0;
  for_each_cache_subdir([&](uint8_t i) {
    level_2_files += counters.get_offsetted(Statistic::subdir_files_base, i);
    level_2_size_kibibyte +=
      counters.get_offsetted(Statistic::subdir_size_kibibyte_base, i);
  });
  return level_2_files == counters.get(Statistic::files_in_cache)
         && level_2_size_kibibyte
              == counters.get(Statistic::cache_size_kibibyte);
}

void
LocalStorage::perform_automatic_cleanup()
{
  util::LongLivedLockFileManager lock_manager;
  auto auto_cleanup_lock = get_auto_cleanup_lock();
  if (!auto_cleanup_lock.try_acquire()) {
    // Somebody else is already performing automatic cleanup.
    return;
  }

  // Intentionally not acquiring content locks here to avoid write contention
  // since precision is not important. It doesn't matter if some compilation
  // sneaks in a new result during our calculation - if maximum cache size
  // becomes exceeded it will be taken care of the next time instead.
  auto evaluation = evaluate_cleanup();
  if (!evaluation) {
    // No cleanup needed.
    return;
  }

  auto_cleanup_lock.make_long_lived(lock_manager);

  if (!has_consistent_counters(evaluation->l1_counters)) {
    LOG("Recounting {} due to inconsistent counters", evaluation->l1_path);
    recount_level_1_dir(lock_manager, evaluation->l1_index);
    evaluation->l1_counters = get_stats_file(evaluation->l1_index).read();
  }

  uint8_t largest_level_2_index =
    get_largest_level_2_index(evaluation->l1_counters);

  auto l2_content_lock =
    get_level_2_content_lock(evaluation->l1_index, largest_level_2_index);
  l2_content_lock.make_long_lived(lock_manager);
  if (!l2_content_lock.acquire()) {
    LOG("Failed to acquire content lock for {}/{}",
        evaluation->l1_index,
        largest_level_2_index);
    return;
  }

  // Need to reread the counters again after acquiring the lock since another
  // compilation may have modified the size since evaluation->l1_counters was
  // read.
  auto stats_file = get_stats_file(evaluation->l1_index);
  auto counters = stats_file.read();
  if (!has_consistent_counters(counters)) {
    // The cache_size_kibibyte counter doesn't match the 16
    // subdir_size_kibibyte_base+i counters. This should only happen if an older
    // ccache version (before introduction of the subdir_size_kibibyte_base
    // counters) has modified the cache size after the recount_level_1_dir call
    // above. Bail out now and leave it to the next ccache invocation to clean
    // up the inconsistency.
    LOG("Inconsistent counters in {}, bailing out", evaluation->l1_path);
    return;
  }

  // Since counting files and their sizes is costly, remove more than needed to
  // amortize the cost. Trimming the directory down to 90% of the max size means
  // that statistically about 20% of the directory content will be removed each
  // automatic cleanup (since subdirectories will be between 90% and about 110%
  // filled at steady state).
  //
  // We trim based on number of files instead of size. The main reason for this
  // is to be more forgiving if there are one or a few large cache entries among
  // many smaller. For example, say that there's a single 100 MB entry (maybe
  // the result of a precompiled header) and lots of small 50 kB files as well.
  // If the large file is the oldest in the subdirectory that is chosen for
  // cleanup, only one file would be removed, thus wasting most of the effort of
  // stat-ing all files. On the other hand, if the large file is the newest, all
  // or a large portion of the other files would be removed on cleanup, thus in
  // practice removing much newer entries than the oldest in other
  // subdirectories. By doing cleanup based on the number of files, both example
  // scenarios are improved.
  const uint64_t target_files = static_cast<uint64_t>(
    0.9 * static_cast<double>(evaluation->total_files) / 256);

  auto clean_dir_result = clean_dir(
    get_subdir(evaluation->l1_index, largest_level_2_index), 0, target_files);

  stats_file.update([&](auto& cs) {
    const auto old_files =
      cs.get_offsetted(Statistic::subdir_files_base, largest_level_2_index);
    const auto old_size_kibibyte = cs.get_offsetted(
      Statistic::subdir_size_kibibyte_base, largest_level_2_index);
    const auto new_files = clean_dir_result.after.files;
    const auto new_size_kibibyte = clean_dir_result.after.size / 1024;
    const int64_t cleanups =
      clean_dir_result.after.size != clean_dir_result.before.size ? 1 : 0;

    cs.increment(Statistic::files_in_cache, new_files - old_files);
    cs.increment(Statistic::cache_size_kibibyte,
                 new_size_kibibyte - old_size_kibibyte);
    cs.set_offsetted(
      Statistic::subdir_files_base, largest_level_2_index, new_files);
    cs.set_offsetted(Statistic::subdir_size_kibibyte_base,
                     largest_level_2_index,
                     new_size_kibibyte);
    cs.increment(Statistic::cleanups_performed, cleanups);
  });
}

void
LocalStorage::do_clean_all(const ProgressReceiver& progress_receiver,
                           uint64_t max_size,
                           uint64_t max_files,
                           std::optional<uint64_t> max_age,
                           std::optional<std::string> namespace_)
{
  util::LongLivedLockFileManager lock_manager;

  uint64_t current_size = 0;
  uint64_t current_files = 0;
  if (max_size > 0 || max_files > 0) {
    for_each_cache_subdir([&](uint8_t i) {
      auto counters = get_stats_file(i).read();
      current_size += 1024 * counters.get(Statistic::cache_size_kibibyte);
      current_files += counters.get(Statistic::files_in_cache);
    });
  }

  for_each_cache_subdir(
    progress_receiver, [&](uint8_t l1_index, const auto& l1_progress_receiver) {
      auto acquired_locks =
        acquire_all_level_2_content_locks(lock_manager, l1_index);
      Level1Counters level_1_counters;

      for_each_cache_subdir(
        l1_progress_receiver,
        [&](uint8_t l2_index, const ProgressReceiver& l2_progress_receiver) {
          uint64_t level_2_max_size =
            current_size > max_size ? max_size / 256 : 0;
          uint64_t level_2_max_files =
            current_files > max_files ? max_files / 256 : 0;
          auto clean_dir_result = clean_dir(get_subdir(l1_index, l2_index),
                                            level_2_max_size,
                                            level_2_max_files,
                                            max_age,
                                            namespace_,
                                            l2_progress_receiver);
          uint64_t removed_size =
            clean_dir_result.before.size - clean_dir_result.after.size;
          uint64_t removed_files =
            clean_dir_result.before.files - clean_dir_result.after.files;

          // removed_size/remove_files should never be larger than
          // current_size/current_files, but in case there's some error we
          // certainly don't want to underflow, so better safe than sorry.
          current_size -= std::min(removed_size, current_size);
          current_files -= std::min(removed_files, current_files);

          level_1_counters.level_2_counters[l2_index] = clean_dir_result.after;
          if (clean_dir_result.after.files != clean_dir_result.before.files) {
            ++level_1_counters.cleanups;
          }

          // Fix erroneous files/size counters for raw files in L2 stats files.
          // See also comments in finalize().
          get_stats_file(l1_index, l2_index)
            .update(
              [](auto& cs) {
                cs.set(Statistic::cache_size_kibibyte, 0);
                cs.set(Statistic::files_in_cache, 0);
              },
              StatsFile::OnlyIfChanged::yes);
        });

      set_counters(get_stats_file(l1_index), level_1_counters);
    });
}

std::optional<LocalStorage::EvaluateCleanupResult>
LocalStorage::evaluate_cleanup()
{
  // We trust that the L1 size and files counters are correct, but the L2 size
  // and files counters may be inconsistent if older ccache versions have been
  // used. If all L2 counters are consistent, we choose the L1 directory with
  // the largest L2 directory, otherwise we just choose the largest L1 directory
  // since we can't trust the L2 counters.

  std::vector<StatisticsCounters> counters;
  counters.reserve(16);
  for_each_cache_subdir([&](uint8_t l1_index) {
    counters.emplace_back(get_stats_file(l1_index).read());
  });
  ASSERT(counters.size() == 16);

  uint64_t largest_l1_dir_files = 0;
  uint64_t largest_l2_dir_files = 0;
  uint8_t largest_l1_dir = 0;
  uint8_t l1_dir_with_largest_l2 = 0;
  uint8_t largest_l2_dir = 0;
  bool l2_counters_consistent = true;
  uint64_t total_files = 0;
  uint64_t total_size = 0;
  for_each_cache_subdir([&](uint8_t i) {
    auto l1_files = counters[i].get(Statistic::files_in_cache);
    auto l1_size = 1024 * counters[i].get(Statistic::cache_size_kibibyte);
    total_files += l1_files;
    total_size += l1_size;
    if (l1_files > largest_l1_dir_files) {
      largest_l1_dir_files = l1_files;
      largest_l1_dir = i;
    }

    if (l2_counters_consistent && has_consistent_counters(counters[i])) {
      for_each_cache_subdir([&](uint8_t j) {
        auto l2_files =
          counters[i].get_offsetted(Statistic::subdir_files_base, j);
        if (l2_files > largest_l2_dir_files) {
          largest_l2_dir_files = l2_files;
          l1_dir_with_largest_l2 = i;
          largest_l2_dir = j;
        }
      });
    } else {
      l2_counters_consistent = false;
    }
  });

  std::string max_size_str =
    m_config.max_size() > 0
      ? FMT(", max size {}",
            util::format_human_readable_size(m_config.max_size(),
                                             m_config.size_unit_prefix_type()))
      : "";
  std::string max_files_str =
    m_config.max_files() > 0 ? FMT(", max files {}", m_config.max_files()) : "";
  std::string info_str = FMT("size {}, files {}{}{}",
                             util::format_human_readable_size(
                               total_size, m_config.size_unit_prefix_type()),
                             total_files,
                             max_size_str,
                             max_files_str);
  if ((m_config.max_size() == 0 || total_size <= m_config.max_size())
      && (m_config.max_files() == 0 || total_files <= m_config.max_files())) {
    LOG("No automatic cleanup needed ({})", info_str);
    return std::nullopt;
  }

  LOG("Need to clean up local cache ({})", info_str);

  uint8_t chosen_l1_dir =
    l2_counters_consistent ? l1_dir_with_largest_l2 : largest_l1_dir;
  auto largest_level_1_dir_path = get_subdir(chosen_l1_dir);
  LOG("Choosing {} for cleanup (counters {}, files {}{})",
      largest_level_1_dir_path,
      has_consistent_counters(counters[chosen_l1_dir]) ? "consistent"
                                                       : "inconsistent",
      largest_l1_dir_files,
      l2_counters_consistent
        ? FMT(", subdir {:x} files {}", largest_l2_dir, largest_l2_dir_files)
        : std::string());

  return EvaluateCleanupResult{chosen_l1_dir,
                               largest_level_1_dir_path,
                               counters[chosen_l1_dir],
                               total_files};
}

std::vector<util::LockFile>
LocalStorage::acquire_all_level_2_content_locks(
  util::LongLivedLockFileManager& lock_manager, uint8_t l1_index)
{
  std::vector<util::LockFile> locks;

  for_each_cache_subdir([&](uint8_t l2_index) {
    auto lock = get_level_2_content_lock(l1_index, l2_index);
    lock.make_long_lived(lock_manager);

    // Not much to do on failure except treating the lock as acquired.
    (void)lock.acquire();

    locks.push_back(std::move(lock));
  });

  return locks;
}

void
LocalStorage::clean_internal_tempdir()
{
  const auto now = util::now();
  const auto cleaned_stamp = FMT("{}/.cleaned", m_config.temporary_dir());
  DirEntry cleaned_dir_entry(cleaned_stamp);
  if (cleaned_dir_entry.is_regular_file()
      && cleaned_dir_entry.mtime() + k_tempdir_cleanup_interval >= now) {
    // No cleanup needed.
    return;
  }

  LOG("Cleaning up {}", m_config.temporary_dir());
  core::ensure_dir_exists(m_config.temporary_dir());

  auto remove_old = [now](const auto& de) {
    if (de.is_directory()) {
      return;
    }
    if (de && de.mtime() + k_tempdir_cleanup_interval < now) {
      LOG("Removing {} (mtime: {})",
          de.path(),
          util::format_iso8601_timestamp(de.mtime()));
      auto result = fs::remove(de.path());
      if (!result) {
        LOG("Removal failed: {}", result.error().message());
      }
    }
  };
  if (auto r = util::traverse_directory(m_config.temporary_dir(), remove_old);
      !r) {
    LOG("Failed to clean up {}: {}", m_config.temporary_dir(), r.error());
  }

  if (auto r = util::write_file(cleaned_stamp, ""); !r) {
    LOG("Failed to create {}: {}", cleaned_stamp, r.error());
  }
}

fs::path
LocalStorage::get_path_in_cache(const uint8_t level,
                                const std::string_view name) const
{
  ASSERT(level >= 1 && level <= 8);
  ASSERT(name.length() >= level);

  fs::path path(m_config.cache_dir());
  for (uint8_t i = 0; i < level; ++i) {
    path /= std::string(1, name.at(i));
  }

  return (path / fs::path(name.substr(level))).string();
}

fs::path
LocalStorage::get_lock_path(const std::string& name) const
{
  auto path = m_config.cache_dir() / "lock" / name;
  core::ensure_dir_exists(path.parent_path());
  return path;
}

util::LockFile
LocalStorage::get_auto_cleanup_lock() const
{
  return util::LockFile(get_lock_path("auto_cleanup"));
}

util::LockFile
LocalStorage::get_level_2_content_lock(const Hash::Digest& key) const
{
  return get_level_2_content_lock(key[0] >> 4, key[0] & 0xF);
}

util::LockFile
LocalStorage::get_level_2_content_lock(uint8_t l1_index, uint8_t l2_index) const
{
  return util::LockFile(
    get_lock_path(FMT("subdir_{:x}{:x}", l1_index, l2_index)));
}

} // namespace storage::local
