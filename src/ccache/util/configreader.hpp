// Copyright (C) 2025 Joel Rosdahl and other contributors
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
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <tl/expected.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace util {

// Reader for configuration files with support for indentation-based line
// continuation.
//
// Continuation rules:
//
// 1. Indented lines (start with whitespace) continue the previous value.
// 2. Comments (start with #) and blank lines are skipped during continuation.
// 3. Non-indented non-comment lines start new entries.
// 4. Continuation lines are stripped and joined with single spaces.
class ConfigReader
{
public:
  struct Error
  {
    size_t line_number;
    std::string message;
  };

  struct RawItem
  {
    size_t line_number;
    std::string_view key;
    size_t value_start_pos; // Position in m_config where value starts
    size_t value_length;
  };

  struct Item
  {
    size_t line_number;
    std::string_view key;
    std::string value; // Normalized: comments/blanks removed, lines joined
  };

  explicit ConfigReader(std::string_view config);

  // Read the next configuration item in raw form. Returns std::nullopt at EOF.
  tl::expected<std::optional<RawItem>, Error> read_next_raw_item();

  // Read the next configuration item with normalized value. Returns
  // std::nullopt at EOF.
  tl::expected<std::optional<Item>, Error> read_next_item();

private:
  std::string_view m_config;
  std::vector<std::string_view> m_lines;
  std::vector<RawItem> m_items;
  std::optional<Error> m_error;
  size_t m_current_item = 0;
};

} // namespace util
