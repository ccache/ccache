// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#ifdef INODE_CACHE_SUPPORTED

#  include "Config.hpp"
#  include "Fd.hpp"
#  include "Finalizer.hpp"
#  include "Stat.hpp"
#  include "Util.hpp"
#  include "ccache.hpp"
#  include "hash.hpp"
#  include "logging.hpp"

#  include <atomic>
#  include <libgen.h>
#  include <sys/mman.h>
#  include <type_traits>

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
  static_cast<int>(InodeCache::ContentType::binary) == 0,
  "Numeric value is part of key, increment version number if changed.");
static_assert(
  static_cast<int>(InodeCache::ContentType::code) == 1,
  "Numeric value is part of key, increment version number if changed.");
static_assert(
  static_cast<int>(InodeCache::ContentType::code_with_sloppy_time_macros) == 2,
  "Numeric value is part of key, increment version number if changed.");
static_assert(
  static_cast<int>(InodeCache::ContentType::precompiled_header) == 3,
  "Numeric value is part of key, increment version number if changed.");

} // namespace

struct InodeCache::Key
{
  ContentType type;
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
#  ifdef HAVE_STRUCT_STAT_ST_MTIM
  timespec st_mtim;
#  else
  time_t st_mtim;
#  endif
#  ifdef HAVE_STRUCT_STAT_ST_CTIM
  timespec st_ctim; // Included for sanity checking.
#  else
  time_t st_ctim; // Included for sanity checking.
#  endif
  off_t st_size; // Included for sanity checking.
  bool sloppy_time_macros;
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
    cc_log("Failed to open inode cache %s: %s",
           inode_cache_file.c_str(),
           strerror(errno));
    return false;
  }
  bool is_nfs;
  if (Util::is_nfs_fd(*fd, &is_nfs) == 0 && is_nfs) {
    cc_log(
      "Inode cache not supported because the cache file is located on nfs: %s",
      inode_cache_file.c_str());
    return false;
  }
  SharedRegion* sr = reinterpret_cast<SharedRegion*>(mmap(
    nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0));
  fd.close();
  if (sr == reinterpret_cast<void*>(-1)) {
    cc_log("Failed to mmap %s: %s", inode_cache_file.c_str(), strerror(errno));
    return false;
  }
  // Drop the file from disk if the found version is not matching. This will
  // allow a new file to be generated.
  if (sr->version != k_version) {
    cc_log(
      "Dropping inode cache because found version %u does not match expected"
      " version %u",
      sr->version,
      k_version);
    munmap(sr, sizeof(SharedRegion));
    unlink(inode_cache_file.c_str());
    return false;
  }
  m_sr = sr;
  if (m_config.debug()) {
    cc_log("inode cache file loaded: %s", inode_cache_file.c_str());
  }
  return true;
}

bool
InodeCache::hash_inode(const char* path, ContentType type, Digest& digest)
{
  Stat stat = Stat::stat(path);
  if (!stat) {
    cc_log("Could not stat %s: %s", path, strerror(stat.error_number()));
    return false;
  }

  Key key;
  memset(&key, 0, sizeof(Key));
  key.type = type;
  key.st_dev = stat.device();
  key.st_ino = stat.inode();
  key.st_mode = stat.mode();
#  ifdef HAVE_STRUCT_STAT_ST_MTIM
  key.st_mtim = stat.mtim();
#  else
  key.st_mtim = stat.mtime();
#  endif
#  ifdef HAVE_STRUCT_STAT_ST_CTIM
  key.st_ctim = stat.ctim();
#  else
  key.st_ctim = stat.ctime();
#  endif
  key.st_size = stat.size();

  digest = hash_buffer_once(&key, sizeof(Key));
  return true;
}

InodeCache::Bucket*
InodeCache::acquire_bucket(uint32_t index)
{
  Bucket* bucket = &m_sr->buckets[index];
  int err = pthread_mutex_lock(&bucket->mt);
#  ifdef PTHREAD_MUTEX_ROBUST
  if (err == EOWNERDEAD) {
    if (m_config.debug()) {
      ++m_sr->errors;
    }
    err = pthread_mutex_consistent(&bucket->mt);
    if (err) {
      cc_log(
        "Can't consolidate stale mutex at index %u: %s", index, strerror(err));
      cc_log("Consider removing the inode cache file if the problem persists");
      return nullptr;
    }
    cc_log("Wiping bucket at index %u because of stale mutex", index);
    memset(bucket->entries, 0, sizeof(Bucket::entries));
  } else {
#  endif
    if (err) {
      cc_log("Failed to lock mutex at index %u: %s", index, strerror(err));
      cc_log("Consider removing the inode cache file if problem persists");
      ++m_sr->errors;
      return nullptr;
    }
#  ifdef PTHREAD_MUTEX_ROBUST
  }
#  endif
  return bucket;
}

InodeCache::Bucket*
InodeCache::acquire_bucket(const Digest& key_digest)
{
  uint32_t hash;
  Util::big_endian_to_int(key_digest.bytes(), hash);
  return acquire_bucket(hash % k_num_buckets);
}

void
InodeCache::release_bucket(Bucket* bucket)
{
  pthread_mutex_unlock(&bucket->mt);
}

bool
InodeCache::create_new_file(const std::string& filename)
{
  cc_log("Creating a new inode cache");

  // Create the new file to a temporary name to prevent other processes from
  // mapping it before it is fully initialized.
  auto temp_fd_and_path = Util::create_temp_fd(filename);
  const auto& temp_path = temp_fd_and_path.second;

  Fd temp_fd(temp_fd_and_path.first);
  Finalizer temp_file_remover([=] { unlink(temp_path.c_str()); });

  bool is_nfs;
  if (Util::is_nfs_fd(*temp_fd, &is_nfs) == 0 && is_nfs) {
    cc_log(
      "Inode cache not supported because the cache file would be located on"
      " nfs: %s",
      filename.c_str());
    return false;
  }
  int err = Util::fallocate(*temp_fd, sizeof(SharedRegion));
  if (err) {
    cc_log("Failed to allocate file space for inode cache: %s", strerror(err));
    return false;
  }
  SharedRegion* sr =
    reinterpret_cast<SharedRegion*>(mmap(nullptr,
                                         sizeof(SharedRegion),
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED,
                                         *temp_fd,
                                         0));
  if (sr == reinterpret_cast<void*>(-1)) {
    cc_log("Failed to mmap new inode cache: %s", strerror(errno));
    return false;
  }

  // Initialize new shared region.
  sr->version = k_version;
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
#  ifdef PTHREAD_MUTEX_ROBUST
  pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
#  endif
  for (uint32_t i = 0; i < k_num_buckets; ++i) {
    pthread_mutex_init(&sr->buckets[i].mt, &mattr);
  }

  munmap(sr, sizeof(SharedRegion));
  temp_fd.close();

  // link() will fail silently if a file with the same name already exists.
  // This will be the case if two processes try to create a new file
  // simultaneously. Thus close the current file handle and reopen a new one,
  // which will make us use the first created file even if we didn't win the
  // race.
  if (link(temp_path.c_str(), filename.c_str()) != 0) {
    cc_log("Failed to link new inode cache: %s", strerror(errno));
    return false;
  }

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
InodeCache::get(const char* path,
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

  Bucket* bucket = acquire_bucket(key_digest);

  if (!bucket) {
    return false;
  }

  bool found = false;

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
  release_bucket(bucket);

  cc_log("inode cache %s: %s", found ? "hit" : "miss", path);

  if (m_config.debug()) {
    if (found) {
      ++m_sr->hits;
    } else {
      ++m_sr->misses;
    }
    cc_log(
      "accumulated stats for inode cache: hits=%ld, misses=%ld, errors=%ld",
      static_cast<long>(m_sr->hits.load()),
      static_cast<long>(m_sr->misses.load()),
      static_cast<long>(m_sr->errors.load()));
  }
  return found;
}

bool
InodeCache::put(const char* path,
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

  Bucket* bucket = acquire_bucket(key_digest);

  if (!bucket) {
    return false;
  }

  memmove(&bucket->entries[1],
          &bucket->entries[0],
          sizeof(Entry) * (k_num_entries - 1));

  bucket->entries[0].key_digest = key_digest;
  bucket->entries[0].file_digest = file_digest;
  bucket->entries[0].return_value = return_value;

  release_bucket(bucket);

  cc_log("inode cache insert: %s", path);

  return true;
}

bool
InodeCache::drop()
{
  std::string file = get_file();
  if (unlink(file.c_str()) != 0) {
    return false;
  }
  if (m_sr) {
    munmap(m_sr, sizeof(SharedRegion));
    m_sr = nullptr;
  }
  return true;
}

std::string
InodeCache::get_file()
{
  return fmt::format("{}/inode-cache.v{}", m_config.temporary_dir(), k_version);
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

#endif
