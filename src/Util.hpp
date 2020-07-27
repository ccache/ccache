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

using ProgressReceiver = std::function<void(double /*progress*/)>;
using CacheFileVisitor = std::function<void(std::shared_ptr<CacheFile>)>;
using SubdirVisitor =
  std::function<void(const std::string& /*dir_path*/,
                     const ProgressReceiver& /*progress_receiver*/)>;
using TraverseVisitor =
  std::function<void(const std::string& path, bool is_dir)>;
using SubstringEditor =
  std::function<void(nonstd::string_view::size_type pos, std::string& substr)>;

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

// Clone, hard link or copy a file from `source` to `dest` depending on settings
// in `ctx`. If cloning or hard linking cannot and should not be done the file
// will be copied instead. Returns true if successful otherwise false.
bool clone_hard_link_or_copy_file(const Context& ctx,
                                  const std::string& source,
                                  const std::string& dest,
                                  bool via_tmp_file);

// Compute the length of the longest directory path that is common to paths
// `dir` (a directory) and `path` (any path).
size_t common_dir_prefix_length(nonstd::string_view dir,
                                nonstd::string_view path);

// Create a directory if needed, including its parents if needed.
//
// Returns true if the directory exists or could be created, otherwise false.
bool create_dir(nonstd::string_view dir);

// Get directory name of path.
nonstd::string_view dir_name(nonstd::string_view path);

// Return true if suffix is a suffix of string.
bool ends_with(nonstd::string_view string, nonstd::string_view suffix);

// Expand all instances of $VAR or ${VAR}, where VAR is an environment variable,
// in `str`. Throws `Error` if one of the environment variables.
[[gnu::warn_unused_result]] std::string
expand_environment_variables(const std::string& str);

// Extends file size to at least new_size by calling posix_fallocate() if
// supported, otherwise by writing zeros last to the file.
//
// Note that existing holes are not filled in case posix_fallocate() is not
// supported.
//
// Returns 0 on success, an error number otherwise.
int fallocate(int fd, long new_size);

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

// Format a hexadecimal string representing `size` bytes of `data`. The returned
// string will be `2 * size` long.
std::string format_hex(const uint8_t* data, size_t size);

// Format `size` as a human-readable string.
std::string format_human_readable_size(uint64_t size);

// Format `size` as a parsable string.
std::string format_parsable_size_with_suffix(uint64_t size);

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

// Test if a file is on nfs.
//
// Sets is_nfs to the result if fstatfs is available and no error occurred.
//
// Returns 0 if is_nfs was set, -1 if fstatfs is not available or errno if an
// error occurred.
int is_nfs_fd(int fd, bool* is_nfs);

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

// Parse `duration`, an unsigned 32-bit integer with d (days) or s (seconds)
// suffix, into seconds. Throws `Error` on error.
uint32_t parse_duration(const std::string& duration);

// Parse a string into a signed integer.
//
// Throws `Error` on error.
int parse_int(const std::string& value);

// Parse a "size value", i.e. a string that can end in k, M, G, T (10-based
// suffixes) or Ki, Mi, Gi, Ti (2-based suffixes). For backward compatibility, K
// is also recognized as a synonym of k. Throws `Error` on parse error.
uint64_t parse_size(const std::string& value);

// Parse a string into an unsigned 32-bit integer.
//
// Throws `Error` on error.
uint32_t parse_uint32(const std::string& value);

// Return `path`'s content as a string. If `size_hint` is not 0 then assume that
// `path` has this size (this saves system calls).
//
// Throws `Error` on error. The description contains the error message without
// the path.
std::string read_file(const std::string& path, size_t size_hint = 0);

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

// Send `text` to STDERR_FILENO, optionally stripping ANSI color sequences if
// `strip_colors` is true. Throws `Error` on error.
void send_to_stderr(const std::string& text, bool strip_colors);

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

// Returns a copy of string with the specified ANSI CSI sequences removed.
[[gnu::warn_unused_result]] std::string
strip_ansi_csi_seqs(nonstd::string_view string);

// Strip whitespace from left and right side of a string.
[[gnu::warn_unused_result]] std::string
strip_whitespace(const std::string& string);

// Convert a string to lowercase.
[[gnu::warn_unused_result]] std::string
to_lowercase(nonstd::string_view string);

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

// Write `data` to `path`. The file will be opened according to `open_mode`,
// which always will include `std::ios::out` even if not specified at the call
// site.
//
// Throws `Error` on error. The description contains the error message without
// the path.
void write_file(const std::string& path,
                const std::string& data,
                std::ios_base::openmode open_mode = std::ios::binary);

} // namespace Util
