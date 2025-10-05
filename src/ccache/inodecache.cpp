// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include "inodecache.hpp"

#include <ccache/config.hpp>
#include <ccache/hash.hpp>
#include <ccache/util/conversion.hpp>
#include <ccache/util/defer.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/temporaryfile.hpp>

#include <fcntl.h>

#ifndef _WIN32
#  include <libgen.h>
#  include <sched.h>
#  include <unistd.h>
#endif

#ifdef HAVE_LINUX_FS_H
#  include <linux/magic.h>
#  include <sys/statfs.h>
#elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
#  include <sys/mount.h>
#  include <sys/param.h>
#endif

#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

namespace fs = util::filesystem;

using namespace std::literals::chrono_literals;

// The inode cache resides on a file that is mapped into shared memory by
// running processes. It is implemented as a two level structure, where the top
// level is a hash table consisting of buckets. Each bucket contains entries
// that are sorted in LRU order. Entries map from keys representing files to
// cached hash results.
//
// Concurrent access is guarded by a mutex in each bucket.
//
// Current cache size is fixed and the given constants are considered large
// enough for most projects. The size could be made configurable if there is a
// demand for it.

namespace {

// The version number corresponds to the format of the cache entries and to
// semantics of the key fields.
//
// Note: The key is hashed using the main hash algorithm, so the version number
// does not need to be incremented if said algorithm is changed (except if the
// digest size changes since that affects the entry format).
const uint32_t k_version = 2;

// Note: Increment the version number if constants affecting storage size are
// changed.
const uint32_t k_num_buckets = 32 * 1024;
const uint32_t k_num_entries = 4;

// Maximum time the spin lock loop will try before giving up.
const std::chrono::seconds k_max_lock_duration{5};

// The memory-mapped file may reside on a filesystem with compression. Memory
// accesses to the file risk crashing if such a filesystem gets full, so stop
// using the inode cache well before this happens.
const uint64_t k_min_fs_mib_left = 100; // 100 MiB

// How long a filesystem space check is valid before we make a new one.
const std::chrono::seconds k_fs_space_check_valid_duration{1};

static_assert(std::tuple_size<Hash::Digest>() == 20,
              "Increment version number if size of digest is changed.");
static_assert(std::is_trivially_copyable_v<Hash::Digest>,
              "Digest is expected to be trivially copyable.");

static_assert(
  static_cast<int>(InodeCache::ContentType::raw) == 0,
  "Numeric value is part of key, increment version number if changed.");
static_assert(
  static_cast<int>(InodeCache::ContentType::checked_for_temporal_macros) == 1,
  "Numeric value is part of key, increment version number if changed.");

bool
fd_is_on_known_to_work_file_system(int fd)
{
#ifndef _WIN32
  bool known_to_work = false;
  struct statfs buf;
  if (fstatfs(fd, &buf) != 0) {
    LOG("fstatfs failed: {}", strerror(errno));
  } else {
#  ifdef HAVE_LINUX_FS_H
    // statfs's f_type field is a signed 32-bit integer on some platforms. Large
    // values therefore cause narrowing warnings, so cast the value to a large
    // unsigned type.
    const auto f_type = static_cast<uintmax_t>(buf.f_type);
    switch (f_type) {
      // Is a filesystem you know works with the inode cache missing in this
      // list? Please submit an issue or pull request to the ccache project.
    case 0x9123683e: // BTRFS_SUPER_MAGIC
    case 0xef53:     // EXT2_SUPER_MAGIC
    case 0x01021994: // TMPFS_MAGIC
    case 0x58465342: // XFS_SUPER_MAGIC
      known_to_work = true;
      break;
    default:
      LOG("Filesystem type 0x{:x} not known to work for the inode cache",
          f_type);
    }
#  elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME) // macOS X and some BSDs
    static const std::vector<std::string> known_to_work_filesystems = {
      // Is a filesystem you know works with the inode cache missing in this
      // list? Please submit an issue or pull request to the ccache project.
      "apfs",
      "tmpfs",
      "ufs",
      "xfs",
      "zfs",
    };
    if (std::find(known_to_work_filesystems.begin(),
                  known_to_work_filesystems.end(),
                  buf.f_fstypename)
        != known_to_work_filesystems.end()) {
      known_to_work = true;
    } else {
      LOG("Filesystem type {} not known to work for the inode cache",
          buf.f_fstypename);
    }
#  else
#    error Inconsistency: INODE_CACHE_SUPPORTED is set but we should not get here
#  endif
  }
  return known_to_work;
#else
  HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  // Try to get information about remote protocol for this file. If the call
  // succeeds, this is a remote file. If the call fails with invalid parameter
  // error, consider whether it is a local file.
  FILE_REMOTE_PROTOCOL_INFO infos;
  if (GetFileInformationByHandleEx(
        file, FileRemoteProtocolInfo, &infos, sizeof(infos))) {
    return false;
  }

  if (GetLastError() == ERROR_INVALID_PARAMETER) {
    return true;
  }

  return false;
#endif
}

bool
spin_lock(std::atomic<pid_t>& owner_pid, const pid_t self_pid)
{
  pid_t prev_pid = 0;
  pid_t lock_pid = 0;
  bool reset_timer = false;
  auto lock_time = std::chrono::steady_clock::now();
  while (true) {
    for (int i = 0; i < 10000; ++i) {
      lock_pid = owner_pid.load(std::memory_order_relaxed);
      if (lock_pid == 0
          && owner_pid.compare_exchange_weak(
            lock_pid, self_pid, std::memory_order_acquire)) {
        return true;
      }

      if (prev_pid != lock_pid) {
        // Check for changing PID here so ABA locking is detected with better
        // probability.
        prev_pid = lock_pid;
        reset_timer = true;
      }
      std::this_thread::yield();
    }
    // If everything is OK, we should never hit this.
    if (reset_timer) {
      lock_time = std::chrono::steady_clock::now();
      reset_timer = false;
    } else if (std::chrono::steady_clock::now() - lock_time
               > k_max_lock_duration) {
      return false;
    }
  }
}

void
spin_unlock(std::atomic<pid_t>& owner_pid)
{
  owner_pid.store(0, std::memory_order_release);
}

} // namespace

struct InodeCache::Key
{
  ContentType type;
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
  timespec st_mtim;
  timespec st_ctim; // Included for sanity checking.
  off_t st_size;    // Included for sanity checking.
};

struct InodeCache::Entry
{
  Hash::Digest key_digest;  // Hashed key
  Hash::Digest file_digest; // Cached file hash
  int return_value;         // Cached return value
};

struct InodeCache::Bucket
{
  std::atomic<pid_t> owner_pid;
  Entry entries[k_num_entries];
};

struct InodeCache::SharedRegion
{
  uint32_t version;
  std::atomic<int64_t> hits;
  std::atomic<int64_t> misses;
  std::atomic<int64_t> errors;
  Bucket buckets[k_num_buckets];
};

bool
InodeCache::mmap_file(const fs::path& path)
{
  m_sr = nullptr;
  m_map.unmap();
  m_fd = util::Fd(open(util::pstr(path).c_str(), O_RDWR));
  if (!m_fd) {
    LOG("Failed to open inode cache {}: {}", path, strerror(errno));
    return false;
  }
  if (!fd_is_on_known_to_work_file_system(*m_fd)) {
    return false;
  }

  auto map = util::MemoryMap::map(*m_fd, sizeof(SharedRegion));
  if (!map) {
    LOG("Failed to map inode cache file {}: {}", path, map.error());
    return false;
  }

  SharedRegion* sr = reinterpret_cast<SharedRegion*>(map->ptr());

  // Drop the file from disk if the found version is not matching. This will
  // allow a new file to be generated.
  if (sr->version != k_version) {
    LOG(
      "Dropping inode cache because found version {} does not match expected"
      " version {}",
      sr->version,
      k_version);
    map->unmap();
    m_fd.close();
    std::ignore = util::remove(path);
    return false;
  }
  m_map = std::move(*map);
  m_sr = sr;
  if (m_config.debug()) {
    LOG("Inode cache file loaded: {}", path);
  }
  return true;
}

bool
InodeCache::hash_inode(const fs::path& path,
                       ContentType type,
                       Hash::Digest& digest)
{
  util::DirEntry de(path);
  if (!de.exists()) {
    LOG("Could not stat {}: {}", path, strerror(de.error_number()));
    return false;
  }

  // See comment for InodeCache::InodeCache why this check is done.
  auto now = util::now();
  if (now - de.ctime() < m_min_age || now - de.mtime() < m_min_age) {
    LOG("Too new ctime or mtime of {}, not considering for inode cache", path);
    return false;
  }

  Key key;
  memset(&key, 0, sizeof(Key));
  key.type = type;
  key.st_dev = de.device();
  key.st_ino = de.inode();
  key.st_mode = de.mode();
  // Note: Manually copying sec and nsec of mtime and ctime to prevent copying
  // the padding bytes.
  auto mtime = util::to_timespec(de.mtime());
  key.st_mtim.tv_sec = mtime.tv_sec;
  key.st_mtim.tv_nsec = mtime.tv_nsec;
  auto ctime = util::to_timespec(de.ctime());
  key.st_ctim.tv_sec = ctime.tv_sec;
  key.st_ctim.tv_nsec = ctime.tv_nsec;
  key.st_size = de.size();

  Hash hash;
  hash.hash({reinterpret_cast<const uint8_t*>(&key), sizeof(key)});
  digest = hash.digest();
  return true;
}

bool
InodeCache::with_bucket(const Hash::Digest& key_digest,
                        const BucketHandler& bucket_handler)
{
  uint32_t hash;
  util::big_endian_to_int(key_digest.data(), hash);
  const uint32_t index = hash % k_num_buckets;
  Bucket* bucket = &m_sr->buckets[index];
  bool acquired_lock = spin_lock(bucket->owner_pid, m_self_pid);
  while (!acquired_lock) {
    LOG("Dropping inode cache file because of stale mutex at index {}", index);
    if (!drop() || !initialize()) {
      return false;
    }
    if (m_config.debug()) {
      ++m_sr->errors;
    }
    bucket = &m_sr->buckets[index];
    acquired_lock = spin_lock(bucket->owner_pid, m_self_pid);
  }
  try {
    bucket_handler(bucket);
  } catch (...) {
    spin_unlock(bucket->owner_pid);
    throw;
  }
  spin_unlock(bucket->owner_pid);
  return true;
}

bool
InodeCache::create_new_file(const fs::path& path)
{
  // Create the new file to a temporary name to prevent other processes from
  // mapping it before it is fully initialized.
  auto tmp_file = util::TemporaryFile::create(path);
  if (!tmp_file) {
    LOG("Failed to created inode cache file: {}", tmp_file.error());
    return false;
  }

  DEFER(unlink(util::pstr(tmp_file->path).c_str()));

  if (!fd_is_on_known_to_work_file_system(*tmp_file->fd)) {
    return false;
  }

  if (auto result = util::fallocate(*tmp_file->fd, sizeof(SharedRegion));
      !result) {
    LOG("Failed to allocate file space for inode cache: {}", result.error());
    return false;
  }

  auto map = util::MemoryMap::map(*tmp_file->fd, sizeof(SharedRegion));
  if (!map) {
    LOG("Failed to mmap new inode cache: {}", map.error());
    return false;
  }

  SharedRegion* sr = reinterpret_cast<SharedRegion*>(map->ptr());

  // Initialize new shared region.
  sr->version = k_version;
  for (auto& bucket : sr->buckets) {
    bucket.owner_pid = 0;
    memset(bucket.entries, 0, sizeof(Bucket::entries));
  }

  sr = nullptr;
  map->unmap();
  tmp_file->fd.close();

#ifndef _WIN32
  // link() will fail silently if a file with the same name already exists.
  // This will be the case if two processes try to create a new file
  // simultaneously. Thus close the current file handle and reopen a new one,
  // which will make us use the first created file even if we didn't win the
  // race.
  if (auto result = fs::create_hard_link(tmp_file->path, path); !result) {
    LOG("Failed to link new inode cache: {}", result.error());
    return false;
  }
#else
  if (MoveFileA(util::pstr(tmp_file->path).c_str(), util::pstr(path).c_str())
      == 0) {
    unsigned error = GetLastError();
    if (error == ERROR_FILE_EXISTS) {
      // Not an error, another process won the race. Remove the file we just
      // created.
      DeleteFileA(util::pstr(tmp_file->path).c_str());
      LOG("Another process created inode cache {}", path);
      return true;
    } else {
      LOG("Failed to move new inode cache: {}", error);
      return false;
    }
  }
#endif

  LOG("Created a new inode cache {}", path);
  return true;
}

bool
InodeCache::initialize()
{
  if (m_failed || !m_config.inode_cache()) {
    return false;
  }

  if (m_fd) {
    auto now = std::chrono::time_point<std::chrono::steady_clock>();
    if (now > m_last_fs_space_check + k_fs_space_check_valid_duration) {
      m_last_fs_space_check = now;

      uint64_t free_space = 0;
#ifndef _WIN32
      struct statfs buf;
      if (fstatfs(*m_fd, &buf) != 0) {
        LOG("fstatfs failed: {}", strerror(errno));
        return false;
      }
      free_space = static_cast<uint64_t>(buf.f_bavail) * 512;
#else
      ULARGE_INTEGER free_space_for_user{};

      if (GetDiskFreeSpaceExA(util::pstr(m_config.temporary_dir()).c_str(),
                              &free_space_for_user,
                              nullptr,
                              nullptr)
          == 0) {
        LOG("GetDiskFreeSpaceExA failed: {}", GetLastError());
        return false;
      }
      free_space = free_space_for_user.QuadPart;
#endif
      if (free_space < k_min_fs_mib_left * 1024 * 1024) {
        LOG("Filesystem has less than {} MiB free space, not using inode cache",
            k_min_fs_mib_left);
        return false;
      }
    }
  }

  if (m_sr) {
    return true;
  }

  fs::path path = get_path();
  if (m_sr || mmap_file(path)) {
    return true;
  }

  // Try to create a new cache if we failed to map an existing file.
  create_new_file(path);

  // Concurrent processes could try to create new files simultaneously and the
  // file that actually landed on disk will be from the process that won the
  // race. Thus we try to open the file from disk instead of reusing the file
  // handle to the file we just created.
  if (mmap_file(path)) {
    return true;
  }

  m_failed = true;
  return false;
}

InodeCache::InodeCache(const Config& config, std::chrono::nanoseconds min_age)
  : m_config(config),
    // CCACHE_DISABLE_INODE_CACHE_MIN_AGE is only for testing purposes; see
    // test/suites/inode_cache.bash.
    m_min_age(getenv("CCACHE_DISABLE_INODE_CACHE_MIN_AGE") ? 0ns : min_age),
    m_self_pid(getpid())
{
}

InodeCache::~InodeCache()
{
  if (m_sr) {
    LOG("Accumulated stats for inode cache: hits={}, misses={}, errors={}",
        m_sr->hits.load(),
        m_sr->misses.load(),
        m_sr->errors.load());
  }
}

bool
InodeCache::available(int fd)
{
  return fd_is_on_known_to_work_file_system(fd);
}

std::optional<std::pair<HashSourceCodeResult, Hash::Digest>>
InodeCache::get(const fs::path& path, ContentType type)
{
  if (!initialize()) {
    return std::nullopt;
  }

  Hash::Digest key_digest;
  if (!hash_inode(path, type, key_digest)) {
    return std::nullopt;
  }

  std::optional<HashSourceCodeResult> result;
  Hash::Digest file_digest;
  const bool success = with_bucket(key_digest, [&](const auto bucket) {
    for (uint32_t i = 0; i < k_num_entries; ++i) {
      if (bucket->entries[i].key_digest == key_digest) {
        if (i > 0) {
          Entry tmp = bucket->entries[i];
          memmove(&bucket->entries[1], &bucket->entries[0], sizeof(Entry) * i);
          bucket->entries[0] = tmp;
        }

        file_digest = bucket->entries[0].file_digest;
        result =
          HashSourceCodeResult::from_bitmask(bucket->entries[0].return_value);
        break;
      }
    }
  });
  if (!success) {
    return std::nullopt;
  }

  if (m_config.debug()) {
    LOG("Inode cache {}: {}", result ? "hit" : "miss", path);
    if (result) {
      ++m_sr->hits;
    } else {
      ++m_sr->misses;
    }
  }
  if (result) {
    return std::make_pair(*result, file_digest);
  } else {
    return std::nullopt;
  }
}

bool
InodeCache::put(const fs::path& path,
                ContentType type,
                const Hash::Digest& file_digest,
                HashSourceCodeResult return_value)
{
  if (!initialize()) {
    return false;
  }

  Hash::Digest key_digest;
  if (!hash_inode(path, type, key_digest)) {
    return false;
  }

  const bool success = with_bucket(key_digest, [&](const auto bucket) {
    memmove(&bucket->entries[1],
            &bucket->entries[0],
            sizeof(Entry) * (k_num_entries - 1));

    bucket->entries[0].key_digest = key_digest;
    bucket->entries[0].file_digest = file_digest;
    bucket->entries[0].return_value = return_value.to_bitmask();
  });

  if (!success) {
    return false;
  }

  if (m_config.debug()) {
    LOG("Inode cache insert: {}", path);
  }
  return true;
}

bool
InodeCache::drop()
{
  m_sr = nullptr;
  m_map.unmap();
  m_fd.close();
  fs::path path = get_path();
  if (!fs::remove(path)) {
    return false;
  }
  LOG("Dropped inode cache {}", path);
  return true;
}

fs::path
InodeCache::get_path()
{
  const uint8_t arch_bits = 8 * sizeof(void*);
  return m_config.temporary_dir()
         / FMT("inode-cache-{}.v{}", arch_bits, k_version);
}

int64_t
InodeCache::get_hits()
{
  return initialize() ? m_sr->hits.load() : -1;
}

int64_t
InodeCache::get_misses()
{
  return initialize() ? m_sr->misses.load() : -1;
}

int64_t
InodeCache::get_errors()
{
  return initialize() ? m_sr->errors.load() : -1;
}
