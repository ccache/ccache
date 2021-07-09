// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <third_party/nonstd/string_view.hpp>

#include <string>
#include <vector>

namespace util {

// Return whether `path` is absolute.
bool is_absolute_path(nonstd::string_view path);

// Return whether `path` includes at least one directory separator.
inline bool
is_full_path(nonstd::string_view path)
{
#ifdef _WIN32
  if (path.find('\\') != nonstd::string_view::npos) {
    return true;
  }
#endif
  return path.find('/') != nonstd::string_view::npos;
}

// Split a list of paths (such as the content of $PATH on Unix platforms or
// %PATH% on Windows platforms) into paths.
std::vector<std::string> split_path_list(nonstd::string_view path_list);

// Make `path` an absolute path.
std::string to_absolute_path(nonstd::string_view path);

} // namespace util
