// Copyright (C) 2022 Joel Rosdahl and other contributors
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

#include "ShowIncludesParser.hpp"

#include <Context.hpp>
#include <Util.hpp>
#include <util/string.hpp>

namespace core::ShowIncludesParser {

std::vector<std::string_view>
tokenize(std::string_view file_content, std::string_view prefix)
{
  // -showIncludes output is written to stdout together with other messages.
  // Every line of it is '<prefix> <spaces> <file>', prefix is 'Note: including
  // file:' in English but can be localized.

  std::vector<std::string_view> result;
  // This will split at each \r or \n, but that simply means there will be empty
  // "lines".
  for (std::string_view line : Util::split_into_views(file_content, "\r\n")) {
    if (util::starts_with(line, prefix)) {
      size_t pos = prefix.size();
      while (pos < line.size() && isspace(line[pos])) {
        ++pos;
      }
      std::string_view include = line.substr(pos);
      if (!include.empty()) {
        result.push_back(include);
      }
    }
  }
  return result;
}

} // namespace core::ShowIncludesParser
