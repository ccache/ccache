// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "InodeCache.hpp"

#include "Config.hpp"
#include "Digest.hpp"
#include "Fd.hpp"
#include "Finalizer.hpp"
#include "Hash.hpp"
#include "Logging.hpp"
#include "Stat.hpp"
#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "fmtmacros.hpp"

#include <fcntl.h>
#include <libgen.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef HAVE_LINUX_FS_H
#  include <linux/magic.h>
#  include <sys/statfs.h>
#elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
#  include <sys/mount.h>
#  include <sys/param.h>
#endif

#include <atomic>
#include <type_traits>

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
const uint32_t k_version = 1;

// Note: Increment the version number if constants affecting storage size are
// changed.
const uint32_t k_num_buckets = 32 * 1024;
const uint32_t k_num_entries = 4;

static_assert(Digest::size() == 20,
              "Increment version number if size of digest is changed.");
static_assert(std::is_trivially_copyable<Digest>::value,
              "Digest is expected to be trivially copyable.");

static_assert(
  static_cast<int>(InodeCache::ContentType::raw) == 0,
  "Numeric value is part of key, increment version number if changed.");
static_assert(
  static_cast<int>(InodeCache::ContentType::checked_for_temporal_macros) == 1,
  "Numeric value is part of key, increment version number if changed.");

const void* MMAP_FAILED = reinterpret_cast<void*>(-1); // NOLINT: Must cast here

bool
fd_is_on_known_to_work_file_system(int fd)
{
  bool known_to_work = false;
  struct statfs buf;
  if (fstatfs(fd, &buf) != 0) {
    LOG("fstatfs failed: {}", strerror(errno));
  } else {
#ifdef HAVE_LINUX_FS_H
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
#elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME) // macOS X and some BSDs
    static const std::vector<std::string> known_to_work_filesystems = {
      // Is a filesystem you know works with the inode cache missing in this
      // list? Please submit an issue or pull request to the ccache project.
      "apfs",
      "xfs",
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
#else
#  error Inconsistency: INODE_CACHE_SUPPORTED is set but we should not get here
#endif
  }
  if (!known_to_work) {
    LOG_RAW("Not using the inode cache");
  }
  return known_to_work;
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
  Digest key_digest;  // Hashed key
  Digest file_digest; // Cached file hash
  int return_value;   // Cached return value
};

struct InodeCache::Bucket
{
  pthread_mutex_t mt;
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
InodeCache::mmap_file(const std::string& inode_cache_file)
{
  if (m_sr) {
    munmap(m_sr, sizeof(SharedRegion));
    m_sr = nullptr;
  }
  Fd fd(open(inode_cache_file.c_str(), O_RDWR));
  if (!fd) {
    LOG("Failed to open inode cache {}: {}", inode_cache_file, strerror(errno));
    return false;
  }
  if (!fd_is_on_known_to_work_file_system(*fd)) {
    return false;
  }
  SharedRegion* sr = reinterpret_cast<SharedRegion*>(mmap(
    nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0));
  fd.close();
  if (sr == MMAP_FAILED) {
    LOG("Failed to mmap {}: {}", inode_cache_file, strerror(errno));
    return false;
  }
  // Drop the file from disk if the found version is not matching. This will
  // allow a new file to be generated.
  if (sr->version != k_version) {
    LOG(
      "Dropping inode cache because found version {} does not match expected"
      " version {}",
      sr->version,
      k_version);
    munmap(sr, sizeof(SharedRegion));
    unlink(inode_cache_file.c_str());
    return false;
  }
  m_sr = sr;
  if (m_config.debug()) {
    LOG("Inode cache file loaded: {}", inode_cache_file);
  }
  return true;
}

bool
InodeCache::hash_inode(const std::string& path,
                       ContentType type,
                       Digest& digest)
{
  Stat stat = Stat::stat(path);
  if (!stat) {
    LOG("Could not stat {}: {}", path, strerror(stat.error_number()));
    return false;
  }

  Key key;
  memset(&key, 0, sizeof(Key));
  key.type = type;
  key.st_dev = stat.device();
  key.st_ino = stat.inode();
  key.st_mode = stat.mode();
  key.st_mtim = stat.mtime().to_timespec();
  key.st_ctim = stat.ctime().to_timespec();
  key.st_size = stat.size();

  Hash hash;
  hash.hash(&key, sizeof(Key));
  digest = hash.digest();
  return true;
}

bool
InodeCache::with_bucket(const Digest& key_digest,
                        const BucketHandler& bucket_handler)
{
  uint32_t hash;
  Util::big_endian_to_int(key_digest.bytes(), hash);
  const uint32_t index = hash % k_num_buckets;
  Bucket* bucket = &m_sr->buckets[index];
  int err = pthread_mutex_lock(&bucket->mt);
#ifdef HAVE_PTHREAD_MUTEX_ROBUST
  if (err == EOWNERDEAD) {
    if (m_config.debug()) {
      ++m_sr->errors;
    }
    err = pthread_mutex_consistent(&bucket->mt);
    if (err) {
      LOG(
        "Can't consolidate stale mutex at index {}: {}", index, strerror(err));
      LOG_RAW("Consider removing the inode cache file if the problem persists");
      return false;
    }
    LOG("Wiping bucket at index {} because of stale mutex", index);
    memset(bucket->entries, 0, sizeof(Bucket::entries));
  } else {
#endif
    if (err != 0) {
      LOG("Failed to lock mutex at index {}: {}", index, strerror(err));
      LOG_RAW("Consider removing the inode cache file if problem persists");
      ++m_sr->errors;
      return false;
    }
#ifdef HAVE_PTHREAD_MUTEX_ROBUST
  }
#endif

  try {
    bucket_handler(bucket);
  } catch (...) {
    pthread_mutex_unlock(&bucket->mt);
    throw;
  }
  pthread_mutex_unlock(&bucket->mt);
  return true;
}

bool
InodeCache::create_new_file(const std::string& filename)
{
  // Create the new file to a temporary name to prevent other processes from
  // mapping it before it is fully initialized.
  TemporaryFile tmp_file(filename);

  Finalizer temp_file_remover([&] { unlink(tmp_file.path.c_str()); });

  if (!fd_is_on_known_to_work_file_system(*tmp_file.fd)) {
    return false;
  }
  int err = Util::fallocate(*tmp_file.fd, sizeof(SharedRegion));
  if (err) {
    LOG("Failed to allocate file space for inode cache: {}", strerror(err));
    return false;
  }
  SharedRegion* sr =
    reinterpret_cast<SharedRegion*>(mmap(nullptr,
                                         sizeof(SharedRegion),
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED,
                                         *tmp_file.fd,
                                         0));
  if (sr == MMAP_FAILED) {
    LOG("Failed to mmap new inode cache: {}", strerror(errno));
    return false;
  }

  // Initialize new shared region.
  sr->version = k_version;
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
#ifdef HAVE_PTHREAD_MUTEX_ROBUST
  pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
#endif
  for (auto& bucket : sr->buckets) {
    pthread_mutex_init(&bucket.mt, &mattr);
  }

  munmap(sr, sizeof(SharedRegion));
  tmp_file.fd.close();

  // link() will fail silently if a file with the same name already exists.
  // This will be the case if two processes try to create a new file
  // simultaneously. Thus close the current file handle and reopen a new one,
  // which will make us use the first created file even if we didn't win the
  // race.
  if (link(tmp_file.path.c_str(), filename.c_str()) != 0) {
    LOG("Failed to link new inode cache: {}", strerror(errno));
    return false;
  }

  LOG("Created a new inode cache {}", filename);
  return true;
}

bool
InodeCache::initialize()
{
  if (m_failed || !m_config.inode_cache()) {
    return false;
  }

  if (m_sr) {
    return true;
  }

  std::string filename = get_file();
  if (m_sr || mmap_file(filename)) {
    return true;
  }

  // Try to create a new cache if we failed to map an existing file.
  create_new_file(filename);

  // Concurrent processes could try to create new files simultaneously and the
  // file that actually landed on disk will be from the process that won the
  // race. Thus we try to open the file from disk instead of reusing the file
  // handle to the file we just created.
  if (mmap_file(filename)) {
    return true;
  }

  m_failed = true;
  return false;
}

InodeCache::InodeCache(const Config& config) : m_config(config)
{
}

InodeCache::~InodeCache()
{
  if (m_sr) {
    munmap(m_sr, sizeof(SharedRegion));
  }
}

bool
InodeCache::available(int fd)
{
  return fd_is_on_known_to_work_file_system(fd);
}

bool
InodeCache::get(const std::string& path,
                ContentType type,
                Digest& file_digest,
                int* return_value)
{
  if (!initialize()) {
    return false;
  }

  Digest key_digest;
  if (!hash_inode(path, type, key_digest)) {
    return false;
  }

  bool found = false;
  const bool success = with_bucket(key_digest, [&](const auto bucket) {
    for (uint32_t i = 0; i < k_num_entries; ++i) {
      if (bucket->entries[i].key_digest == key_digest) {
        if (i > 0) {
          Entry tmp = bucket->entries[i];
          memmove(&bucket->entries[1], &bucket->entries[0], sizeof(Entry) * i);
          bucket->entries[0] = tmp;
        }

        file_digest = bucket->entries[0].file_digest;
        if (return_value) {
          *return_value = bucket->entries[0].return_value;
        }
        found = true;
        break;
      }
    }
  });
  if (!success) {
    return false;
  }

  LOG("Inode cache {}: {}", found ? "hit" : "miss", path);

  if (m_config.debug()) {
    if (found) {
      ++m_sr->hits;
    } else {
      ++m_sr->misses;
    }
    LOG("Accumulated stats for inode cache: hits={}, misses={}, errors={}",
        m_sr->hits.load(),
        m_sr->misses.load(),
        m_sr->errors.load());
  }
  return found;
}

bool
InodeCache::put(const std::string& path,
                ContentType type,
                const Digest& file_digest,
                int return_value)
{
  if (!initialize()) {
    return false;
  }

  Digest key_digest;
  if (!hash_inode(path, type, key_digest)) {
    return false;
  }

  const bool success = with_bucket(key_digest, [&](const auto bucket) {
    memmove(&bucket->entries[1],
            &bucket->entries[0],
            sizeof(Entry) * (k_num_entries - 1));

    bucket->entries[0].key_digest = key_digest;
    bucket->entries[0].file_digest = file_digest;
    bucket->entries[0].return_value = return_value;
  });

  if (!success) {
    return false;
  }

  LOG("Inode cache insert: {}", path);

  return true;
}

bool
InodeCache::drop()
{
  std::string file = get_file();
  if (unlink(file.c_str()) != 0) {
    return false;
  }
  LOG("Dropped inode cache {}", file);
  if (m_sr) {
    munmap(m_sr, sizeof(SharedRegion));
    m_sr = nullptr;
  }
  return true;
}

std::string
InodeCache::get_file()
{
  const uint8_t arch_bits = 8 * sizeof(void*);
  return FMT(
    "{}/inode-cache-{}.v{}", m_config.temporary_dir(), arch_bits, k_version);
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
