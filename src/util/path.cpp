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

#include "path.hpp"

#include <Util.hpp>
#include <fmtmacros.hpp>

#ifdef _WIN32
const char k_path_delimiter[] = ";";
#else
const char k_path_delimiter[] = ":";
#endif

namespace util {

bool
is_absolute_path(nonstd::string_view path)
{
#ifdef _WIN32
  if (path.length() >= 2 && path[1] == ':'
      && (path[2] == '/' || path[2] == '\\')) {
    return true;
  }
#endif
  return !path.empty() && path[0] == '/';
}

std::vector<std::string>
split_path_list(nonstd::string_view path_list)
{
  return Util::split_into_strings(path_list, k_path_delimiter);
}

std::string
to_absolute_path(nonstd::string_view path)
{
  if (util::is_absolute_path(path)) {
    return std::string(path);
  } else {
    return Util::normalize_absolute_path(
      FMT("{}/{}", Util::get_actual_cwd(), path));
  }
}

} // namespace util
