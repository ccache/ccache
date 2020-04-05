// Copyright (C) 2019 Joel Rosdahl and other contributors
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
#  include "Util.hpp"
#  include "ccache.hpp"
#  include "hash.hpp"
#  include "logging.hpp"

#  include <atomic>
#  include <errno.h>
#  include <fcntl.h>
#  include <libgen.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>

namespace InodeCache {

namespace {

// The inode cache resides on a file that is mapped into shared memory by
// running processes. It is implemented as a two level structure, where the
// top level is a hash table consisting of buckets. Each bucket contains entries
// that are sorted in lru order. Entries maps from keys representing files to
// cached hash results.
//
// Concurrent access is guarded by a mutex in each bucket.
//
// Current cache size is fixed and the given constants are considered large
// enough for most projects. The size could be made configurable if there is a
// demand for it.
const uint32_t k_version = 1;

// Increment version number if constants affecting storage size are changed.
const uint32_t k_num_buckets = 32 * 1024;
const uint32_t k_num_entries = 4;

static_assert(sizeof(digest::bytes) == 20,
              "Increment version number if size of digest  is changed.");

const char k_default_basename[] = "/inode_cache";

struct Key
{
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

struct Entry
{
  digest key_digest;  // Hashed key
  digest file_digest; // Cached file hash
  int return_value;   // Cached return value
};

struct Bucket
{
  pthread_mutex_t mt;
  int32_t hits;
  int32_t misses;
  Entry entries[k_num_entries];
};

struct SharedRegion
{
  uint32_t version;
  std::atomic<int64_t> errors;
  Bucket buckets[k_num_buckets];
};

SharedRegion* g_sr;

bool
mmap_file(const std::string& inode_cache_file)
{
  if (g_sr) {
    munmap(g_sr, sizeof(SharedRegion));
    g_sr = nullptr;
  }
  int fd = open(inode_cache_file.c_str(), O_RDWR);
  if (fd < 0) {
    cc_log("Failed to open inode cache %s: %s",
           inode_cache_file.c_str(),
           strerror(errno));
    return false;
  }
  SharedRegion* sr = reinterpret_cast<SharedRegion*>(mmap(
    nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  close(fd);
  if (sr == reinterpret_cast<void*>(-1)) {
    fprintf(stderr,
            "Failed to mmap %s: %s\n",
            inode_cache_file.c_str(),
            strerror(errno));
    return false;
  }
  // Drop the file from disk if the found version is not matching. This will
  // allow a new file to be generated.
  if (sr->version != k_version) {
    cc_log(
      "Dropping inode cache because found version %u does not match expected "
      "version %u",
      sr->version,
      k_version);
    munmap(sr, sizeof(SharedRegion));
    unlink(inode_cache_file.c_str());
    return false;
  }
  g_sr = sr;
  return true;
}

bool
hash_inode(const Config& config, const char* path, digest* digest)
{
  struct stat st_buf;
  if (stat(path, &st_buf)) {
    cc_log("Could not stat %s: %s", path, strerror(errno));
    return false;
  }

  Key key;
  memset(&key, 0, sizeof(Key));
  key.st_dev = st_buf.st_dev;
  key.st_ino = st_buf.st_ino;
  key.st_mode = st_buf.st_mode;
#  ifdef HAVE_STRUCT_STAT_ST_MTIM
  key.st_mtim = st_buf.st_mtim;
#  else
  key.st_mtim = st_buf.st_mtime;
#  endif
#  ifdef HAVE_STRUCT_STAT_ST_CTIM
  key.st_ctim = st_buf.st_ctim;
#  else
  key.st_ctim = st_buf.st_ctime;
#  endif
  key.st_size = st_buf.st_size;
  key.sloppy_time_macros = config.sloppiness() & SLOPPY_TIME_MACROS;

  struct hash* hash = hash_init();
  hash_buffer(hash, &key, sizeof(Key));
  hash_result_as_bytes(hash, digest);
  hash_free(hash);
  return true;
}

Bucket*
acquire_bucket(uint32_t index)
{
  Bucket* bucket = &g_sr->buckets[index];
  int err = pthread_mutex_lock(&bucket->mt);
#  ifdef PTHREAD_MUTEX_ROBUST
  if (err == EOWNERDEAD) {
    ++g_sr->errors;
    err = pthread_mutex_consistent(&bucket->mt);
    if (err) {
      fprintf(stderr,
              "Can't consolidate stale mutex at index %u: %s\n",
              index,
              strerror(err));
      fprintf(stderr,
              "Consider removing the inode cache file if preblem persists.\n");
      return nullptr;
    }
    cc_log("Whiping bucket at index %u because of stale mutex.\n", index);
    memset(bucket->entries, 0, sizeof(Bucket::entries));
  } else {
#  endif
    if (err) {
      fprintf(
        stderr, "Failed to lock mutex at index %u: %s\n", index, strerror(err));
      fprintf(stderr,
              "Consider removing the inode cache file if preblem persists.\n");
      ++g_sr->errors;
      return nullptr;
    }
#  ifdef PTHREAD_MUTEX_ROBUST
  }
#  endif
  return bucket;
}

Bucket*
acquire_bucket(const digest& key_digest)
{
  uint32_t hash;
  Util::big_endian_to_int(key_digest.bytes, hash);
  return acquire_bucket(hash % k_num_buckets);
}

void
release_bucket(Bucket* bucket)
{
  pthread_mutex_unlock(&bucket->mt);
}

std::string
get_file_from_config(const Config& config)
{
  return config.inode_cache_file().empty()
           ? config.cache_dir() + k_default_basename
           : config.inode_cache_file();
}

bool
create_new_file(const std::string& filename)
{
  cc_log("Creating a new inode cache");

  char path_buf[PATH_MAX];
  snprintf(path_buf, PATH_MAX, "%s", filename.c_str());
  const char* dname = dirname(path_buf);
  if (!Util::create_dir(dname)) {
    fprintf(stderr,
            "Failed to create directory %s for inode cache: %s\n",
            dname,
            strerror(errno));
  }
#  ifdef O_TMPFILE
  // Create the new file as invisible to prevent other processes from mapping it
  // before it is fully initialized.
  int fd = open(dname, O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    fprintf(stderr,
            "Failed to create new inode cache in directory %s: %s\n",
            dname,
            strerror(errno));
    return false;
  }
  snprintf(path_buf, PATH_MAX, "/proc/self/fd/%d", fd);
#  else
  // Create the new file to a a temporary name to prevent other processes from
  // mapping it before it is fully initialized.
  snprintf(path_buf, PATH_MAX, "%s_XXXXXX", filename.c_str());
  int fd = mkstemp(path_buf);
  if (fd < 0) {
    fprintf(stderr,
            "Failed to create new inode cache to temporary file %s: %s\n",
            path_buf,
            strerror(errno));
    return false;
  }
#  endif
#  ifdef HAVE_POSIX_FALLOCATE
  if (posix_fallocate(fd, 0, sizeof(SharedRegion))) {
    fprintf(stderr,
            "Failed to allocate file space for inode cache: %s\n",
            strerror(errno));
    close(fd);
    return false;
  }
#  else
  void* buf = calloc(sizeof(SharedRegion), 1);
  if (!buf) {
    fprintf(stderr, "Failed to allocate temporary memory for inode cache.");
    close(fd);
    return false;
  }
  if (write(fd, buf, sizeof(SharedRegion)) != sizeof(SharedRegion)) {
    fprintf(stderr,
            "Failed to allocate file space for inode cache: %s\n",
            strerror(errno));
    free(buf);
    close(fd);
    return false;
  }
  free(buf);
#  endif
  SharedRegion* sr = reinterpret_cast<SharedRegion*>(mmap(
    nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (sr == reinterpret_cast<void*>(-1)) {
    fprintf(stderr, "Failed to mmap new inode cache: %s\n", strerror(errno));
    close(fd);
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
  for (uint32_t i = 0; i < k_num_buckets; ++i)
    pthread_mutex_init(&sr->buckets[i].mt, &mattr);

  munmap(sr, sizeof(SharedRegion));

  // linkat() will fail silently if a file with the same name already exists.
  // This will be the case if two processes tries to create a new file
  // simultaneously. Thus close the current file handle and reopen a new one,
  // which will make us using the first created file also if we didn't win the
  // race.
  if (linkat(
        AT_FDCWD, path_buf, AT_FDCWD, filename.c_str(), AT_SYMLINK_FOLLOW)) {
    cc_log("Failed to link new inode cache: %s", strerror(errno));
#  ifndef O_TMPFILE
    unlink(path_buf);
#  endif
    close(fd);
    return false;
  }

#  ifndef O_TMPFILE
  unlink(path_buf);
#  endif
  close(fd);
  return true;
}

bool
initialize(const Config& config)
{
  if (!config.inode_cache())
    return false;

  if (g_sr)
    return true;

  std::string filename = get_file_from_config(config);
  if (g_sr || mmap_file(filename))
    return true;

  // Try to create a new cache if we failed to map an existing file.
  create_new_file(filename);

  // Concurrent processes could try to create new files simultaneously and the
  // file that actually landed on disk will be from the process that won the
  // race. Thus we try to open the file from disk instead of reusing the file
  // handle to the file we just created.
  return mmap_file(filename);
}

} // namespace

bool
get(const Config& config,
    const char* path,
    digest* file_digest,
    int* return_value)
{
  if (!initialize(config))
    return false;

  digest key_digest;
  if (!hash_inode(config, path, &key_digest))
    return false;

  Bucket* bucket = acquire_bucket(key_digest);

  if (!bucket)
    return false;

  for (uint32_t i = 0; i < k_num_entries; ++i) {
    if (digests_equal(&bucket->entries[i].key_digest, &key_digest)) {
      if (i) {
        Entry tmp;
        memcpy(&tmp, &bucket->entries[i], sizeof(Entry));
        memmove(&bucket->entries[1], &bucket->entries[0], sizeof(Entry) * i);
        memcpy(&bucket->entries[0], &tmp, sizeof(Entry));
      }

      *file_digest = bucket->entries[0].file_digest;
      *return_value = bucket->entries[0].return_value;
      ++bucket->hits;
      release_bucket(bucket);

      return true;
    }
  }
  ++bucket->misses;
  release_bucket(bucket);
  return false;
}

bool
put(const Config& config,
    const char* path,
    const digest& file_digest,
    int return_value)
{
  if (!initialize(config))
    return false;

  digest key_digest;
  if (!hash_inode(config, path, &key_digest))
    return false;

  Bucket* bucket = acquire_bucket(key_digest);

  if (!bucket)
    return false;

  memmove(&bucket->entries[1],
          &bucket->entries[0],
          sizeof(Entry) * (k_num_entries - 1));

  bucket->entries[0].key_digest = key_digest;
  bucket->entries[0].file_digest = file_digest;
  bucket->entries[0].return_value = return_value;

  release_bucket(bucket);

  return true;
}

bool
zero_stats(const Config& config)
{
  if (!initialize(config))
    return false;
  for (uint32_t i = 0; i < k_num_buckets; ++i) {
    Bucket* bucket = acquire_bucket(i);
    if (!bucket)
      return false;

    bucket->hits = 0;
    bucket->misses = 0;
    release_bucket(bucket);
  }
  g_sr->errors = 0;
  return true;
}

bool
drop(const Config& config)
{
  std::string file = get_file(config);
  if (file.empty() || unlink(file.c_str()))
    return false;
  if (g_sr) {
    munmap(g_sr, sizeof(SharedRegion));
    g_sr = nullptr;
  }
  return true;
}

std::string
get_file(const Config& config)
{
  std::string filename = get_file_from_config(config);
  struct stat st_buf;
  if (!stat(filename.c_str(), &st_buf))
    return filename;
  return std::string();
}

int64_t
get_hits(const Config& config)
{
  if (!initialize(config))
    return -1;
  int64_t sum = 0;
  for (uint32_t i = 0; i < k_num_buckets; ++i) {
    Bucket* bucket = acquire_bucket(i);
    if (!bucket)
      return -1;

    sum += bucket->hits;
    release_bucket(bucket);
  }
  return sum;
}

int64_t
get_misses(const Config& config)
{
  if (!initialize(config))
    return -1;
  int64_t sum = 0;
  for (uint32_t i = 0; i < k_num_buckets; ++i) {
    Bucket* bucket = acquire_bucket(i);
    if (!bucket)
      return -1;

    sum += bucket->misses;
    release_bucket(bucket);
  }
  return sum;
}

int64_t
get_errors(const Config& config)
{
  return initialize(config) ? g_sr->errors.load() : -1;
}

} // namespace InodeCache
#endif
