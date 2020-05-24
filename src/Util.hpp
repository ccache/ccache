// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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
#include "Config.hpp"

#include "third_party/nonstd/string_view.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Util {

using ProgressReceiver = std::function<void(double)>;
using CacheFileVisitor = std::function<void(std::shared_ptr<CacheFile>)>;
using SubdirVisitor =
  std::function<void(const std::string& /*dir_path*/,
                     const ProgressReceiver& /*progress_receiver*/)>;
using TraverseVisitor =
  std::function<void(const std::string& path, bool is_dir)>;

enum class UnlinkLog { log_failure, ignore_failure };

// Get base name of path.
nonstd::string_view base_name(nonstd::string_view path);

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

// Remove the extension via `remove_extension()`, then add `new_ext`. `new_ext`
// should start with a dot, no extra dot is inserted.
std::string change_extension(nonstd::string_view path,
                             nonstd::string_view new_ext);

// Compute the length of the longest directory path that is common to paths
// `dir` (a directory) and `path` (any path).
size_t common_dir_prefix_length(nonstd::string_view dir,
                                nonstd::string_view path);

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

// Return current working directory (CWD) as returned from getcwd(3) (i.e.,
// normalized path without symlink parts). Returns the empty string on error.
std::string get_actual_cwd();

// Return current working directory (CWD) by reading the environment variable
// PWD (thus keeping any symlink parts in the path and potentially ".." or "//"
// parts). If PWD does not resolve to the same i-node as `actual_cwd` then
// `actual_cwd` is returned instead.
std::string get_apparent_cwd(const std::string& actual_cwd);

// Return the file extension (including the dot) as a view into `path`. If
// `path` has no file extension, an empty string_view is returned.
nonstd::string_view get_extension(nonstd::string_view path);

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

// Compute a relative path from `dir` (an absolute path to a directory) to
// `path` (an absolute path). Assumes that both `dir` and `path` are normalized.
// The algorithm does *not* follow symlinks, so the result may not actually
// resolve to the same file as `path`.
std::string get_relative_path(nonstd::string_view dir,
                              nonstd::string_view path);

// Join `cache_dir`, a '/', `name`, and `suffix` into a single path and return
// it. Additionally `levels` single-character, '/'-separated subpaths are split
// from the beginning of `name` before joining them all.
//
// `levels` must be less than the length of `name` and in the interval [1,8].
//
// E.g. "ABCDEF" and ".foo" will become "/ccache/A/B/CDEF.foo" when the cache
// directory is "/ccache" and cache dir levels is 2.
std::string get_path_in_cache(nonstd::string_view cache_dir,
                              uint32_t levels,
                              nonstd::string_view name,
                              nonstd::string_view suffix);

// Return a shortened view into the base name of `path`. This view starts at the
// beginning of the base name and ends at either the position the first dot, or
// `max_length`, or the length of the base name, whichever is the shortest.
nonstd::string_view get_truncated_base_name(nonstd::string_view path,
                                            size_t max_length);

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

// Return whether `path` is absolute.
bool is_absolute_path(nonstd::string_view path);

// Return whether `ch` is a directory separator, i.e. '/' on POSIX systems and
// '/' or '\\' on Windows systems.
inline bool
is_dir_separator(char ch)
{
  return ch == '/'
#ifdef _WIN32
         || ch == '\\'
#endif
    ;
}

// Make a relative path from current working directory to `path` if `path` is
// under the base directory.
std::string make_relative_path(const Context& ctx, nonstd::string_view path);

// Return whether `path` is equal to `dir_prefix_or_file` or if
// `dir_prefix_or_file` is a directory prefix of `path`.
bool matches_dir_prefix_or_file(nonstd::string_view dir_prefix_or_file,
                                nonstd::string_view path);

// Normalize absolute path `path`, not taking symlinks into account.
//
// Normalization here means syntactically removing redundant slashes and
// resolving "." and ".." parts. The algorithm does however *not* follow
// symlinks, so the result may not actually resolve to `path`.
//
// On Windows: Backslashes are replaced with forward slashes.
std::string normalize_absolute_path(nonstd::string_view path);

// Parse a string into an integer.
//
// Throws Error on error.
int parse_int(const std::string& value);

// Read file data as a string.
//
// Throws `Error` on error. The description contains the error message without
// the path.
std::string read_file(const std::string& path);

#ifndef _WIN32
// Like readlink(2) but returns the string (or the empty string on failure).
std::string read_link(const std::string& path);
#endif

// Return a normalized absolute path of `path`. On error (e.g. if the `path`
// doesn't exist) the empty string is returned if return_empty_on_error is true,
// otherwise `path` unmodified.
std::string real_path(const std::string& path,
                      bool return_empty_on_error = false);

// Return a view into `path` containing the given path without the filename
// extension as determined by `get_extension()`.
nonstd::string_view remove_extension(nonstd::string_view path);

// Split `input` into words at any of the characters listed in `separators`.
// These words are a view into `input`; empty words are omitted. `separators`
// must neither be the empty string nor a nullptr.
std::vector<nonstd::string_view> split_into_views(nonstd::string_view input,
                                                  const char* separators);

// Same as `split_into_views` but the words are copied from `input`.
std::vector<std::string> split_into_strings(nonstd::string_view input,
                                            const char* separators);

// Return true if prefix is a prefix of string.
bool starts_with(nonstd::string_view string, nonstd::string_view prefix);

// Strip whitespace from left and right side of a string.
[[gnu::warn_unused_result]] std::string
strip_whitespace(const std::string& string);

// Convert a string to lowercase.
[[gnu::warn_unused_result]] std::string to_lowercase(const std::string& string);

// Traverse `path` recursively (postorder, i.e. files are visited before their
// parent directory).
//
// Throws Error on error.
void traverse(const std::string& path, const TraverseVisitor& visitor);

// Remove `path` (non-directory), NFS safe. Logs according to `unlink_log`.
//
// Returns whether removal was successful. A non-existing `path` is considered
// successful.
bool unlink_safe(const std::string& path,
                 UnlinkLog unlink_log = UnlinkLog::log_failure);

// Remove `path` (non-directory), NFS hazardous. Use only for files that will
// not exist on other systems. Logs according to `unlink_log`.
//
// Returns whether removal was successful. A non-existing `path` is considered
// successful.
bool unlink_tmp(const std::string& path,
                UnlinkLog unlink_log = UnlinkLog::log_failure);

// Remove `path` (and its contents if it's a directory). A non-existing path is
// not considered an error.
//
// Throws Error on error.
void wipe_path(const std::string& path);

// Write file data from a string. The file will be opened according to
// `open_mode`, which always will include `std::ios::out` even if not specified
// at the call site.
//
// Throws `Error` on error. The description contains the error message without
// the path.
void write_file(const std::string& path,
                const std::string& data,
                std::ios_base::openmode open_mode = std::ios::out);

} // namespace Util
