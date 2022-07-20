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

#include "path.hpp"

#include <Util.hpp>
#include <fmtmacros.hpp>

#ifdef _WIN32
const char k_dev_null_path[] = "nul:";
const char k_path_delimiter[] = ";";
#else
const char k_dev_null_path[] = "/dev/null";
const char k_path_delimiter[] = ":";
#endif

namespace util {

const char*
get_dev_null_path()
{
  return k_dev_null_path;
}

bool
is_absolute_path(std::string_view path)
{
#ifdef _WIN32
  if (path.length() >= 2 && path[1] == ':'
      && (path[2] == '/' || path[2] == '\\')) {
    return true;
  }
#endif
  return !path.empty() && path[0] == '/';
}

bool
path_starts_with(std::string_view path, std::string_view prefix)
{
  for (size_t i = 0, j = 0; i < path.length() && j < prefix.length();
       ++i, ++j) {
#ifdef _WIN32
    // Skip escaped backslashes \\\\ as seen by the preprocessor.
    if (i > 0 && path[i] == '\\' && path[i - 1] == '\\') {
      ++i;
    }
    if (j > 0 && prefix[j] == '\\' && prefix[j - 1] == '\\') {
      ++j;
    }

    // Handle back and forward slashes as equal.
    if (path[i] == '/' && prefix[j] == '\\') {
      continue;
    }
    if (path[i] == '\\' && prefix[j] == '/') {
      continue;
    }
#endif
    if (path[i] != prefix[j]) {
      return false;
    }
  }
  return true;
}

std::vector<std::string>
split_path_list(std::string_view path_list)
{
  return Util::split_into_strings(path_list, k_path_delimiter);
}

std::string
to_absolute_path(std::string_view path)
{
  if (util::is_absolute_path(path)) {
    return std::string(path);
  } else {
    return Util::normalize_abstract_absolute_path(
      FMT("{}/{}", Util::get_actual_cwd(), path));
  }
}

std::string
to_absolute_path_no_drive(std::string_view path)
{
  std::string abs_path = to_absolute_path(path);
#ifdef _WIN32
  if (abs_path.length() >= 2 && abs_path[1] == ':') {
    abs_path.erase(0, 2);
  }
#endif
  return abs_path;
}

} // namespace util
