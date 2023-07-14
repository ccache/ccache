// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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
#include <util/TimePoint.hpp>
#include <util/Tokenizer.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Config;
class Context;

namespace Util {

using TraverseVisitor =
  std::function<void(const std::string& path, bool is_dir)>;

enum class UnlinkLog { log_failure, ignore_failure };

// Get base name of path.
std::string_view base_name(std::string_view path);

// Remove the extension via `remove_extension()`, then add `new_ext`. `new_ext`
// should start with a dot, no extra dot is inserted.
std::string change_extension(std::string_view path, std::string_view new_ext);

// Compute the length of the longest directory path that is common to paths
// `dir` (a directory) and `path` (any path).
size_t common_dir_prefix_length(std::string_view dir, std::string_view path);

// Create a directory if needed, including its parents if needed.
//
// Returns true if the directory exists or could be created, otherwise false.
bool create_dir(std::string_view dir);

// Get directory name of path.
std::string_view dir_name(std::string_view path);

// Like create_dir but throws Fatal on error.
void ensure_dir_exists(std::string_view dir);

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
std::string_view get_extension(std::string_view path);

// Return the current user's home directory, or throw `Fatal` if it can't
// be determined.
std::string get_home_directory();

// Return a static string with the current hostname.
const char* get_hostname();

// Compute a relative path from `dir` (an absolute path to a directory) to
// `path` (an absolute path). Assumes that both `dir` and `path` are normalized.
// The algorithm does *not* follow symlinks, so the result may not actually
// resolve to the same file as `path`.
std::string get_relative_path(std::string_view dir, std::string_view path);

// Determine if `path` is an absolute path with prefix, returning the split
// point.
std::optional<size_t> is_absolute_path_with_prefix(std::string_view path);

// Detmine if `path` refers to a ccache executable.
bool is_ccache_executable(std::string_view path);

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
bool is_precompiled_header(std::string_view path);

// Thread-safe version of `localtime(3)`. If `time` is not specified the current
// time of day is used.
std::optional<tm> localtime(std::optional<util::TimePoint> time = {});

// Construct a normalized native path.
//
// Example:
//
//   std::string path = Util::make_path("usr", "local", "bin");
template<typename... T>
std::string
make_path(const T&... args)
{
  return (std::filesystem::path{} / ... / args).lexically_normal().string();
}

// Make a relative path from current working directory (either `actual_cwd` or
// `apparent_cwd`) to `path` if `path` is under `base_dir`.
std::string make_relative_path(const std::string& base_dir,
                               const std::string& actual_cwd,
                               const std::string& apparent_cwd,
                               std::string_view path);

// Like above but with base directory and apparent/actual CWD taken from `ctx`.
std::string make_relative_path(const Context& ctx, std::string_view path);

// Return whether `path` is equal to `dir_prefix_or_file` or if
// `dir_prefix_or_file` is a directory prefix of `path`.
bool matches_dir_prefix_or_file(std::string_view dir_prefix_or_file,
                                std::string_view path);

// Normalize absolute path `path`, not taking symlinks into account.
//
// Normalization here means syntactically removing redundant slashes and
// resolving "." and ".." parts. The algorithm does however *not* follow
// symlinks, so the result may not actually resolve to the same filesystem entry
// as `path` (nor to any existing file system entry for that matter).
//
// On Windows: Backslashes are replaced with forward slashes.
std::string normalize_abstract_absolute_path(std::string_view path);

// Like normalize_abstract_absolute_path, but returns `path` unchanged if the
// normalized result doesn't resolve to the same file system entry as `path`.
std::string normalize_concrete_absolute_path(const std::string& path);

// Return a view into `path` containing the given path without the filename
// extension as determined by `get_extension()`.
std::string_view remove_extension(std::string_view path);

// Send `text` to file descriptor `fd`, optionally stripping ANSI color
// sequences if `ctx.args_info.strip_diagnostics_colors` is true and rewriting
// paths to absolute if `ctx.config.absolute_paths_in_stderr` is true. Throws
// `core::Error` on error.
void send_to_fd(const Context& ctx, std::string_view text, int fd);

// Set the FD_CLOEXEC on file descriptor `fd`. This is a NOP on Windows.
void set_cloexec_flag(int fd);

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
std::vector<std::string_view>
split_into_views(std::string_view string,
                 const char* separators,
                 util::Tokenizer::Mode mode = util::Tokenizer::Mode::skip_empty,
                 util::Tokenizer::IncludeDelimiter include_delimiter =
                   util::Tokenizer::IncludeDelimiter::no);

// Same as `split_into_views` but the tokens are copied from `string`.
std::vector<std::string> split_into_strings(
  std::string_view string,
  const char* separators,
  util::Tokenizer::Mode mode = util::Tokenizer::Mode::skip_empty,
  util::Tokenizer::IncludeDelimiter include_delimiter =
    util::Tokenizer::IncludeDelimiter::no);

// Returns a copy of string with the specified ANSI CSI sequences removed.
[[nodiscard]] std::string strip_ansi_csi_seqs(std::string_view string);

// Convert a string to lowercase.
[[nodiscard]] std::string to_lowercase(std::string_view string);

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

// Remove `path` (and its contents if it's a directory). A nonexistent path is
// not considered an error.
//
// Throws core::Error on error.
void wipe_path(const std::string& path);

} // namespace Util
