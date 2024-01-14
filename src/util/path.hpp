// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <util/string.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace util {

// --- Interface ---

// Return current working directory (CWD) as returned from getcwd(3) (i.e.,
// normalized path without symlink parts). Returns the empty string on error.
std::string actual_cwd();

// Add ".exe" suffix to `program` if it doesn't already end with ".exe", ".bat"
// or ".sh".
std::string add_exe_suffix(const std::string& program);

// Return current working directory (CWD) by reading the environment variable
// PWD (thus keeping any symlink parts in the path and potentially ".." or "//"
// parts). If PWD does not resolve to the same inode as `actual_cwd` then
// `actual_cwd` is returned instead.
std::string apparent_cwd(const std::string& actual_cwd);

const char* get_dev_null_path();

// Return whether `path` is /dev/null or (on Windows) NUL.
bool is_dev_null_path(const std::filesystem::path& path);

// Return whether `path` includes at least one directory separator.
bool is_full_path(std::string_view path);

// Return whether `path` starts with `prefix` considering path specifics on
// Windows
bool path_starts_with(std::string_view path, std::string_view prefix);

// --- Inline implementations ---

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

} // namespace util
