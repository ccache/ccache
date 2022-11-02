// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include <string>
#include <string_view>
#include <vector>

namespace util {

// --- Interface ---

const char* get_dev_null_path();

// Return whether `path` is absolute.
bool is_absolute_path(std::string_view path);

// Return whether `path` includes at least one directory separator.
bool is_full_path(std::string_view path);

// Return whether `path` starts with `prefix` considering path specifics on
// Windows
bool path_starts_with(std::string_view path, std::string_view prefix);

// Split a list of paths (such as the content of $PATH on Unix platforms or
// %PATH% on Windows platforms) into paths.
std::vector<std::string> split_path_list(std::string_view path_list);

// Make `path` an absolute path.
std::string to_absolute_path(std::string_view path);

// Make `path` an absolute path, but do not include Windows drive.
std::string to_absolute_path_no_drive(std::string_view path);

// --- Inline implementations ---

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
