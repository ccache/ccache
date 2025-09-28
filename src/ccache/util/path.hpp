// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/pathstring.hpp>
#ifdef _WIN32
#  include <ccache/util/string.hpp>
#endif

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace util {

// --- Interface ---

// Add ".exe" suffix to `program` if it doesn't already have an extension.
std::string add_exe_suffix(const std::string& program);

// Return a new path with `extension` added to `path` (keeping any existing
// extension).
std::filesystem::path add_extension(const std::filesystem::path& path,
                                    std::string_view extension);

// Return current working directory (CWD) by reading the environment variable
// PWD (thus keeping any symlink parts in the path and potentially ".." or "//"
// parts). If PWD does not resolve to the same inode as `actual_cwd` then
// `actual_cwd` is returned instead.
std::filesystem::path apparent_cwd(const std::filesystem::path& actual_cwd);

const char* get_dev_null_path();

// Return lexically normal `path` without trailing slash.
std::filesystem::path lexically_normal(const std::filesystem::path& path);

// Return whether `path` is /dev/null or (on Windows) NUL.
bool is_dev_null_path(const std::filesystem::path& path);

// Return whether `path` includes at least one directory separator.
bool is_full_path(std::string_view path);

// Make a relative path from current working directory (either `actual_cwd` or
// `apparent_cwd`) to `path` if `path` is under `base_dir`.
std::filesystem::path
make_relative_path(const std::filesystem::path& actual_cwd,
                   const std::filesystem::path& apparent_cwd,
                   const std::filesystem::path& path);

// Construct a normalized native path.
//
// Example:
//
//   std::string path = make_path("usr", "local", "bin");
template<typename... T>
std::filesystem::path
make_path(const T&... args)
{
  return (std::filesystem::path{} / ... / args).lexically_normal();
}

// Return whether `path` starts with `prefix` considering path specifics on
// Windows.
bool path_starts_with(const std::filesystem::path& path,
                      const std::filesystem::path& prefix);

// Return whether `path` starts with any of `prefixes` considering path
// specifics on Windows.
bool path_starts_with(const std::filesystem::path& path,
                      const std::vector<std::filesystem::path>& prefixes);

// Access the underlying path string without having to copy it if
// std::filesystem::path::value_type is char (that is, not wchar_t).
using pstr = PathString;

// Return a new path with `extension` added to `path` (removing any existing
// extension).
std::filesystem::path with_extension(const std::filesystem::path& path,
                                     std::string_view extension);

// --- Inline implementations ---

inline std::filesystem::path
add_extension(const std::filesystem::path& path, std::string_view extension)
{
  std::filesystem::path result(path);
  result += std::filesystem::path(extension);
  return result;
}

inline bool
is_dev_null_path(const std::filesystem::path& path)
{
  return path == "/dev/null"
#ifdef _WIN32
         || util::to_lowercase(path.string()) == "nul"
#endif
    ;
}

inline bool
is_full_path(const std::string_view path)
{
#ifdef _WIN32
  if (path.find('\\') != std::string_view::npos) {
    return true;
  }
#endif
  return path.find('/') != std::string_view::npos;
}

inline std::filesystem::path
with_extension(const std::filesystem::path& path, std::string_view extension)
{
  std::filesystem::path result(path);
  result.replace_extension(extension);
  return result;
}

} // namespace util
