// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <tl/expected.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace util {

// Simple JSON parser that is tailored for parsing MSVC's /sourceDependencies
// files.
//
// Does not support \uXXXX escapes and lots of other things.
class SimpleJsonParser
{
public:
  explicit SimpleJsonParser(std::string_view document);

  // Extract array of strings from the document. `filter` is a jq-like filter
  // (e.g. ".Data.Includes") that locates the string array to extract. The
  // filter syntax currently only supports nested objects.
  tl::expected<std::vector<std::string>, std::string>
  get_string_array(std::string_view filter) const;

private:
  std::string_view m_document;
};

} // namespace util
