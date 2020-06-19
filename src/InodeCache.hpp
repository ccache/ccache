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

#pragma once

#include "system.hpp"

#include "config.h"

#include <stdint.h>
#include <string>

class Config;
class Context;
class Digest;

class InodeCache
{
public:
  // Specifies in which role a file was hashed, since the hash result does not
  // only depend on the actual content but also what we used it for. Source code
  // files are scanned for macros while binary files are not as one example.
  enum class ContentType {
    binary = 0,
    code = 1,
    code_with_sloppy_time_macros = 2,
    precompiled_header = 3,
  };

  InodeCache(const Config& config);
  ~InodeCache();

  // Get saved hash digest and return value from a previous call to
  // hash_source_code_file().
  //
  // Returns true if saved values could be retrieved from the cache, false
  // otherwise.
  bool get(const char* path,
           ContentType type,
           Digest& file_digest,
           int* return_value = nullptr);

  // Put hash digest and return value from a successful call to
  // hash_source_code_file().
  //
  // Returns true if values could be stored in the cache, false otherwise.
  bool put(const char* path,
           ContentType type,
           const Digest& file_digest,
           int return_value = 0);

  // Unmaps the current cache and removes the mapped file from disk.
  //
  // Returns true on success, false otherwise.
  bool drop();

  // Returns name of the persistent file.
  std::string get_file();

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

  bool mmap_file(const std::string& inode_cache_file);
  static bool hash_inode(const char* path, ContentType type, Digest& digest);
  Bucket* acquire_bucket(uint32_t index);
  Bucket* acquire_bucket(const Digest& key_digest);
  static void release_bucket(Bucket* bucket);
  static bool create_new_file(const std::string& filename);
  bool initialize();

  const Config& m_config;
  struct SharedRegion* m_sr = nullptr;
  bool m_failed = false;
};
