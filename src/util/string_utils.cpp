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

#include "string_utils.hpp"

namespace util {

std::pair<nonstd::string_view, nonstd::optional<nonstd::string_view>>
split_once(const nonstd::string_view string, const char split_char)
{
  const size_t sep_pos = string.find(split_char);
  if (sep_pos == nonstd::string_view::npos) {
    return std::make_pair(string, nonstd::nullopt);
  } else {
    return std::make_pair(string.substr(0, sep_pos),
                          string.substr(sep_pos + 1));
  }
}

} // namespace util
