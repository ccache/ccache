// Copyright (C) 2022-2025 Joel Rosdahl and other contributors
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

#include "msvc.hpp"

#include <ccache/context.hpp>
#include <ccache/util/json.hpp>
#include <ccache/util/string.hpp>

namespace compiler {

std::vector<std::string_view>
get_includes_from_msvc_show_includes(std::string_view file_content,
                                     std::string_view prefix)
{
  // /showIncludes output is written to stdout together with other messages.
  // Every line of it is "<prefix> <spaces> <file>" where the prefix is "Note:
  // including file:" in English but can be localized.

  std::vector<std::string_view> result;
  // This will split at each \r or \n, but that simply means there will be empty
  // "lines".
  for (std::string_view line : util::split_into_views(file_content, "\r\n")) {
    if (util::starts_with(line, prefix)) {
      size_t pos = prefix.size();
      while (pos < line.size() && util::is_space(line[pos])) {
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

tl::expected<std::vector<std::string>, std::string>
get_includes_from_msvc_source_deps(std::string_view json_content)
{
  util::SimpleJsonParser parser(json_content);
  return parser.get_string_array(".Data.Includes");
}

} // namespace compiler
