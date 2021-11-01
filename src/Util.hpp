// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include <Stat.hpp>
#include <util/Tokenizer.hpp>

#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

class Context;

namespace Util {

using DataReceiver = std::function<void(const void* data, size_t size)>;
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

// Return `value` adjusted to not be less than `min` and not more than `max`.
template<typename T>
T
clamp(T value, T min, T max)
{
  return std::min(max, std::max(min, value));
}

// Clone a file from `src` to `dest`. If `via_tmp_file` is true, `src` is cloned
// to a temporary file and then renamed to `dest`. Throws `core::Error` on
// error.
void clone_file(const std::string& src,
                const std::string& dest,
                bool via_tmp_file = false);

// Clone, hard link or copy a file from `source` to `dest` depending on settings
// in `ctx`. If cloning or hard linking cannot and should not be done the file
// will be copied instead. Throws `core::Error` on error.
void clone_hard_link_or_copy_file(const Context& ctx,
                                  const std::string& source,
                                  const std::string& dest,
                                  bool via_tmp_file = false);

// Compute the length of the longest directory path that is common to paths
// `dir` (a directory) and `path` (any path).
size_t common_dir_prefix_length(nonstd::string_view dir,
                                nonstd::string_view path);

// Copy all data from `fd_in` to `fd_out`. Throws `core::Error` on error.
void copy_fd(int fd_in, int fd_out);

// Copy a file from `src` to `dest`. If via_tmp_file is true, `src` is copied to
// a temporary file and then renamed to dest. Throws `core::Error` on error.
void copy_file(const std::string& src,
               const std::string& dest,
               bool via_tmp_file = false);

// Create a directory if needed, including its parents if needed.
//
// Returns true if the directory exists or could be created, otherwise false.
bool create_dir(nonstd::string_view dir);

// Get directory name of path.
nonstd::string_view dir_name(nonstd::string_view path);

// Like create_dir but throws Fatal on error.
void ensure_dir_exists(nonstd::string_view dir);

// Expand all instances of $VAR or ${VAR}, where VAR is an environment variable,
// in `str`. Throws `core::Error` if one of the environment variables.
[[nodiscard]] std::string expand_environment_variables(const std::string& str);

// Extends file size to at least new_size by calling posix_fallocate() if
// supported, otherwise by writing zeros last to the file.
//
// Note that existing holes are not filled in case posix_fallocate() is not
// supported.
//
// Returns 0 on success, an error number otherwise.
int fallocate(int fd, long new_size);

// Format `argv` as a simple string for logging purposes. That is, the result is
// not intended to be machine parsable. `argv` must be terminated by a nullptr.
std::string format_argv_for_logging(const char* const* argv);

// Format a hexadecimal string representing `size` bytes of `data`. The returned
// string will be `2 * size` long.
std::string format_base16(const uint8_t* data, size_t size);

// Format a lowercase base32hex string representing `size` bytes of `data`. No
// padding characters will be added.
std::string format_base32hex(const uint8_t* data, size_t size);

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

// Return the current user's home directory, or throw `Fatal` if it can't
// be determined.
std::string get_home_directory();

// Return a static string with the current hostname.
const char* get_hostname();

// Compute a relative path from `dir` (an absolute path to a directory) to
// `path` (an absolute path). Assumes that both `dir` and `path` are normalized.
// The algorithm does *not* follow symlinks, so the result may not actually
// resolve to the same file as `path`.
std::string get_relative_path(nonstd::string_view dir,
                              nonstd::string_view path);

// Hard-link `oldpath` to `newpath`. Throws `core::Error` on error.
void hard_link(const std::string& oldpath, const std::string& newpath);

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

// Return whether `path` represents a precompiled header (see "Precompiled
// Headers" in GCC docs).
bool is_precompiled_header(nonstd::string_view path);

// Thread-safe version of `localtime(3)`. If `time` is not specified the current
// time of day is used.
nonstd::optional<tm> localtime(nonstd::optional<time_t> time = {});

// Make a relative path from current working directory (either `actual_cwd` or
// `apparent_cwd`) to `path` if `path` is under `base_dir`.
std::string make_relative_path(const std::string& base_dir,
                               const std::string& actual_cwd,
                               const std::string& apparent_cwd,
                               nonstd::string_view path);

// Like above but with base directory and apparent/actual CWD taken from `ctx`.
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

// Parse `duration`, an unsigned integer with d (days) or s (seconds) suffix,
// into seconds. Throws `core::Error` on error.
uint64_t parse_duration(const std::string& duration);

// Parse a "size value", i.e. a string that can end in k, M, G, T (10-based
// suffixes) or Ki, Mi, Gi, Ti (2-based suffixes). For backward compatibility, K
// is also recognized as a synonym of k. Throws `core::Error` on parse error.
uint64_t parse_size(const std::string& value);

// Read data from `fd` until end of file and call `data_receiver` with the read
// data. Returns whether reading was successful, i.e. whether the read(2) call
// did not return -1.
bool read_fd(int fd, DataReceiver data_receiver);

// Return `path`'s content as a string. If `size_hint` is not 0 then assume that
// `path` has this size (this saves system calls).
//
// Throws `core::Error` on error. The description contains the error message
// without the path.
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

// Rename `oldpath` to `newpath` (deleting `newpath`). Throws `core::Error` on
// error.
void rename(const std::string& oldpath, const std::string& newpath);

// Detmine if `program_name` is equal to `canonical_program_name`. On Windows,
// this means performing a case insensitive equality check with and without a
// ".exe" suffix. On non-Windows, it is a simple equality check.
bool same_program_name(nonstd::string_view program_name,
                       nonstd::string_view canonical_program_name);

// Send `text` to STDERR_FILENO, optionally stripping ANSI color sequences if
// `ctx.args_info.strip_diagnostics_colors` is true and rewriting paths to
// absolute if `ctx.config.absolute_paths_in_stderr` is true. Throws
// `core::Error` on error.
void send_to_stderr(const Context& ctx, const std::string& text);

// Set the FD_CLOEXEC on file descriptor `fd`. This is a NOP on Windows.
void set_cloexec_flag(int fd);

// Set environment variable `name` to `value`.
void setenv(const std::string& name, const std::string& value);

// Return size change in KiB between `old_stat`  and `new_stat`.
inline int64_t
size_change_kibibyte(const Stat& old_stat, const Stat& new_stat)
{
  return (static_cast<int64_t>(new_stat.size_on_disk())
          - static_cast<int64_t>(old_stat.size_on_disk()))
         / 1024;
}

// Split `string` into tokens at any of the characters in `separators`. These
// tokens are views into `string`. `separators` must neither be the empty string
// nor a nullptr.
std::vector<nonstd::string_view> split_into_views(
  nonstd::string_view string,
  const char* separators,
  util::Tokenizer::Mode mode = util::Tokenizer::Mode::skip_empty);

// Same as `split_into_views` but the tokens are copied from `string`.
std::vector<std::string> split_into_strings(
  nonstd::string_view string,
  const char* separators,
  util::Tokenizer::Mode mode = util::Tokenizer::Mode::skip_empty);

// Returns a copy of string with the specified ANSI CSI sequences removed.
[[nodiscard]] std::string strip_ansi_csi_seqs(nonstd::string_view string);

// Convert a string to lowercase.
[[nodiscard]] std::string to_lowercase(nonstd::string_view string);

// Traverse `path` recursively (postorder, i.e. files are visited before their
// parent directory).
//
// Throws core::Error on error.
void traverse(const std::string& path, const TraverseVisitor& visitor);

// Remove `path` (non-directory), NFS safe. Logs according to `unlink_log`.
//
// Returns whether removal was successful. A nonexistent `path` is considered a
// failure.
bool unlink_safe(const std::string& path,
                 UnlinkLog unlink_log = UnlinkLog::log_failure);

// Remove `path` (non-directory), NFS hazardous. Use only for files that will
// not exist on other systems. Logs according to `unlink_log`.
//
// Returns whether removal was successful. A nonexistent `path` is considered
// successful.
bool unlink_tmp(const std::string& path,
                UnlinkLog unlink_log = UnlinkLog::log_failure);

// Unset environment variable `name`.
void unsetenv(const std::string& name);

// Set mtime of `path` to the current timestamp.
void update_mtime(const std::string& path);

// Remove `path` (and its contents if it's a directory). A nonexistent path is
// not considered an error.
//
// Throws core::Error on error.
void wipe_path(const std::string& path);

// Write `size` bytes from `data` to `fd`. Throws `core::Error` on error.
void write_fd(int fd, const void* data, size_t size);

// Write `data` to `path`. The file will be opened according to `open_mode`,
// which always will include `std::ios::out` even if not specified at the call
// site.
//
// Throws `core::Error` on error. The description contains the error message
// without the path.
void write_file(const std::string& path,
                const std::string& data,
                std::ios_base::openmode open_mode = std::ios::binary);

} // namespace Util
