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

#pragma once

#include "system.hpp"

#include "CacheFile.hpp"

#include "third_party/nonstd/string_view.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Util {

typedef std::function<void(double)> ProgressReceiver;
typedef std::function<void(std::shared_ptr<CacheFile>)> CacheFileVisitor;
typedef std::function<void(const std::string& /*dir_path*/,
                           const ProgressReceiver& /*progress_receiver*/)>
  SubdirVisitor;

// Get base name of path.
nonstd::string_view base_name(nonstd::string_view path);

// Return the file extension (including the dot) as a view into `path`.
// If `path` has no file extension, an empty string_view is returned.
nonstd::string_view get_extension(nonstd::string_view path);

// Return a view into `path` containing the given path without the
// filename extension as determined by `get_extension()`.
nonstd::string_view remove_extension(nonstd::string_view path);

// Remove the extension via `remove_extension()`, then add `new_ext`.
// `new_ext` should start with a dot, no extra dot is inserted.
std::string change_extension(nonstd::string_view path,
                             nonstd::string_view new_ext);

// Return a shortened view into the base name of `path``. This view starts at
// the beginning of the base name and ends at either the position the first dot,
// or `max_length`, or the length of the base name, whichever is the shortest.
nonstd::string_view get_truncated_base_name(nonstd::string_view path,
                                            size_t max_length);

// Get an integer value from bytes in big endian order.
//
// Parameters:
// - buffer: Bytes to read.
// - count: Number of bytes to read.
template<typename T>
void
big_endian_to_int(const uint8_t* buffer, T& value)
{
  value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    value <<= 8;
    value |= buffer[i];
  }
}

template<>
inline void
big_endian_to_int(const uint8_t* buffer, int8_t& value)
{
  value = buffer[0];
}

template<>
inline void
big_endian_to_int(const uint8_t* buffer, uint8_t& value)
{
  value = buffer[0];
}

// Create a directory if needed, including its parents if needed.
//
// Returns true if the directory exists or could be created, otherwise false.
bool create_dir(nonstd::string_view dir);

// Create a unique temporary file.
//
// Parameters:
// - path_prefix: Base path. The resulting filename will be this path plus a
//   unique suffix.
//
// Returns the open file descriptor (in read/write mode) and the actual
// filename.
std::pair<int, std::string> create_temp_fd(nonstd::string_view path_prefix);

// Get directory name of path.
nonstd::string_view dir_name(nonstd::string_view path);

// Return true if suffix is a suffix of string.
bool ends_with(nonstd::string_view string, nonstd::string_view suffix);

// Call a function for each subdir (0-9a-f) in the cache.
//
// Parameters:
// - cache_dir: Path to the cache directory.
// - visitor: Function to call with directory path and progress_receiver as
//   arguments.
// - progress_receiver: Function that will be called for progress updates.
void for_each_level_1_subdir(const std::string& cache_dir,
                             const SubdirVisitor& visitor,
                             const ProgressReceiver& progress_receiver);

// Get a list of files in a level 1 subdirectory of the cache.
//
// The function works under the assumption that directory entries with one
// character names (except ".") are subdirectories and that there are no other
// subdirectories.
//
// Files ignored:
// - CACHEDIR.TAG
// - stats
// - .nfs* (temporary NFS files that may be left for open but deleted files).
//
// Parameters:
// - dir: The directory to traverse recursively.
// - progress_receiver: Function that will be called for progress updates.
// - files: Found files.
void get_level_1_files(const std::string& dir,
                       const ProgressReceiver& progress_receiver,
                       std::vector<std::shared_ptr<CacheFile>>& files);

// Join the global cache directory, a '/', `name`, and `suffix` into a single
// path and return it. Additionally N single-character, '/'-separated subpaths
// are split from the beginning of `name` before joining them all, where N is
// the number of globally configured cache dir levels.
//
// Throws if cache dir levels is greater than the length of `name`.
//
// E.g. "ABCDEF" and ".foo" will become "/ccache/A/B/CDEF.foo" when
// the cache directory is "/ccache" and cache dir levels is 2.
std::string get_path_in_cache(nonstd::string_view name,
                              nonstd::string_view suffix);

// Write bytes in big endian order from an integer value.
//
// Parameters:
// - value: Integer value to read.
// - buffer: Buffer to write bytes to.
template<typename T>
void
int_to_big_endian(T value, uint8_t* buffer)
{
  for (size_t i = 0; i < sizeof(T); ++i) {
    buffer[sizeof(T) - i - 1] = value & 0xFF;
    value >>= 8;
  }
}

template<>
inline void
int_to_big_endian(uint8_t value, uint8_t* buffer)
{
  buffer[0] = value;
}

template<>
inline void
int_to_big_endian(int8_t value, uint8_t* buffer)
{
  buffer[0] = value;
}

// Parse a string into an integer.
//
// Throws Error on error.
int parse_int(const std::string& value);

// Read file data as a string.
//
// Throws Error on error.
std::string read_file(const std::string& path);

// Return true if prefix is a prefix of string.
bool starts_with(nonstd::string_view string, nonstd::string_view prefix);

// Strip whitespace from left and right side of a string.
[[gnu::warn_unused_result]] std::string
strip_whitespace(const std::string& string);

// Convert a string to lowercase.
[[gnu::warn_unused_result]] std::string to_lowercase(const std::string& string);

// Write file data from a string.
//
// Throws Error on error.
void write_file(const std::string& path,
                const std::string& data,
                bool binary = false);

} // namespace Util
