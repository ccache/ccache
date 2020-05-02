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

#ifdef INODE_CACHE_SUPPORTED

#  include <stdint.h>
#  include <string>

class Config;
struct digest;

namespace InodeCache {

// Get saved hash digest and return value from a previous call to
// hash_source_code_file().
//
// Returns true if saved values could be retrieved from the cache, false
// otherwise.
bool get(const Config& config,
         const char* path,
         digest* file_digest,
         int* return_value);

// Put hash digest and return value from a successful call to
// hash_source_code_file().
//
// Returns true if values could be stored in the cache, false otherwise.
bool put(const Config& config,
         const char* path,
         const digest& file_digest,
         int return_value);

// Clears persistent counters.
//
// Returns true on success, false otherwise.
bool zero_stats(const Config& config);

// Unmaps the current cache and removes the mapped file from disk.
//
// Returns true on success, false otherwise.
bool drop(const Config& config);

// Returns name of the persistent file.
std::string get_file(const Config& config);

// Returns total number of cache hits.
int64_t get_hits(const Config& config);

// Returns total number of cache misses.
int64_t get_misses(const Config& config);

// Returns total number of errors.
//
// Currently only lock errors will be counted, since the counter is not
// accessible before the file has been successfully mapped into memory.
int64_t get_errors(const Config& config);

} // namespace InodeCache
#endif
