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

#pragma once

#include <ccache/hash.hpp>
#include <ccache/hashutil.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/memorymap.hpp>

#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

class Config;

class InodeCache
{
public:
  // Specifies in which mode a file was hashed since the hash result does not
  // only depend on the actual content but also on operations that were
  // performed that affect the return value. For example, source code files are
  // normally scanned for macros while binary files are not.
  enum class ContentType {
    // The file was not scanned for temporal macros.
    raw = 0,
    // The file was checked for temporal macros (see check_for_temporal_macros
    // in hashutil).
    checked_for_temporal_macros = 1,
  };

  // `min_age` specifies how old a file must be to be put in the cache. The
  // reason for this is that there is a race condition that consists of these
  // events:
  //
  // 1. A file is written with content C1, size S and timestamp (ctime/mtime) T.
  // 2. Ccache hashes the file content and asks the inode cache to store the
  //    digest with a hash of S and T (and some other data) as the key.
  // 3. The file is quickly thereafter written with content C2 without changing
  //    size S and timestamp T. The timestamp is not updated since the file
  //    writes are made within a time interval smaller than the granularity of
  //    the clock used for file system timestamps. At the time of writing, a
  //    common granularity on a Linux system is 0.004 s (250 Hz).
  // 4. The inode cache is asked for the file digest and the inode cache
  //    delivers a digest of C1 even though the file's content is C2.
  //
  // To avoid the race condition, the inode cache only caches inodes whose
  // timestamp was updated more than `min_age` ago. The default value is a
  // conservative 2 seconds since not all file systems have subsecond
  // resolution.
  InodeCache(const Config& config,
             std::chrono::nanoseconds min_age = std::chrono::seconds(2));
  ~InodeCache();

  // Return whether it's possible to use the inode cache on the filesystem
  // associated with `fd`.
  static bool available(int fd);

  // Get saved hash digest and return value from a previous call to
  // do_hash_file() in hashutil.cpp.
  std::optional<std::pair<HashSourceCodeResult, Hash::Digest>>
  get(const std::filesystem::path& path, ContentType type);

  // Put hash digest and return value from a successful call to do_hash_file()
  // in hashutil.cpp.
  //
  // Returns true if values could be stored in the cache, false otherwise.
  bool put(const std::filesystem::path& path,
           ContentType type,
           const Hash::Digest& file_digest,
           HashSourceCodeResult return_value);

  // Unmaps the current cache and removes the mapped file from disk.
  //
  // Returns true on success, false otherwise.
  bool drop();

  // Returns name of the persistent file.
  std::filesystem::path get_path();

  // Returns total number of cache hits.
  //
  // Counters are incremented in debug mode only.
  int64_t get_hits();

  // Returns total number of cache misses.
  //
  // Counters are incremented in debug mode only.
  int64_t get_misses();

  // Returns total number of errors.
  //
  // Currently only lock errors will be counted, since the counter is not
  // accessible before the file has been successfully mapped into memory.
  //
  // Counters are incremented in debug mode only.
  int64_t get_errors();

private:
  struct Bucket;
  struct Entry;
  struct Key;
  struct SharedRegion;
  using BucketHandler = std::function<void(Bucket* bucket)>;

  bool mmap_file(const std::filesystem::path& path);

  bool hash_inode(const std::filesystem::path& path,
                  ContentType type,
                  Hash::Digest& digest);

  bool with_bucket(const Hash::Digest& key_digest,
                   const BucketHandler& bucket_handler);

  static bool create_new_file(const std::filesystem::path& path);

  bool initialize();

  const Config& m_config;
  std::chrono::nanoseconds m_min_age;
  util::Fd m_fd;
  struct SharedRegion* m_sr = nullptr;
  bool m_failed = false;
  const pid_t m_self_pid;
  std::chrono::time_point<std::chrono::steady_clock> m_last_fs_space_check;
  util::MemoryMap m_map;
};
