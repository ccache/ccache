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

#include "configreader.hpp"

#include <ccache/util/string.hpp>

#include <cctype>

namespace util {

namespace {

bool
is_comment_or_blank(std::string_view line)
{
  std::string stripped = util::strip_whitespace(line);
  return stripped.empty() || stripped[0] == '#';
}

} // namespace

ConfigReader::ConfigReader(std::string_view config)
  : m_config(config),
    m_lines(split_into_views(config, "\n", Tokenizer::Mode::include_empty))
{
  // First pass: find all keys
  for (size_t i = 0; i < m_lines.size(); ++i) {
    std::string_view line = m_lines[i];

    if (!is_comment_or_blank(line)) {
      // Non-comment, non-blank line found
      if (!line.empty() && util::is_space(line[0])) {
        // Indented non-comment line without a preceding key
        if (m_items.empty()) {
          m_error = Error{i + 1, "indented key"};
          return;
        }
      } else {
        // This should be a key=value line
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) {
          // Non-indented line without "=" is an error
          m_error = Error{i + 1, "missing equal sign"};
          return;
        }

        std::string_view key = line.substr(0, eq_pos);
        while (!key.empty() && util::is_space(key.back())) {
          key.remove_suffix(1);
        }

        size_t line_start = static_cast<size_t>(line.data() - m_config.data());
        size_t value_start = line_start + eq_pos + 1;
        while (value_start < m_config.size()
               && util::is_space(m_config[value_start])) {
          ++value_start;
        }

        m_items.push_back(RawItem{i + 1, key, value_start, 0});
      }
    }
  }

  // Second pass: compute value lengths for each key
  for (size_t i = 0; i < m_items.size(); ++i) {
    auto& item = m_items[i];
    size_t line_index = item.line_number - 1; // Convert to 0-based

    // Determine the end line for this item's value
    size_t search_end_line = (i + 1 < m_items.size())
                               ? (m_items[i + 1].line_number - 1)
                               : m_lines.size();

    // Backtrack from search_end_line to skip trailing comments and blank lines
    size_t value_end_line = search_end_line;
    while (value_end_line > line_index + 1) {
      if (!is_comment_or_blank(m_lines[value_end_line - 1])) {
        // Found the last content line
        break;
      }
      --value_end_line;
    }

    std::string_view last_line = m_lines[value_end_line - 1];
    size_t last_line_start =
      static_cast<size_t>(last_line.data() - m_config.data());
    // If the line ends with CR (as in CRLF files split on '\n'), treat the
    // trailing CR as part of the newline and exclude it from the value end.
    size_t last_line_size = last_line.size();
    if (last_line_size > 0 && last_line.back() == '\r') {
      --last_line_size;
    }
    size_t value_end = last_line_start + last_line_size;

    item.value_length = value_end - item.value_start_pos;
  }
}

tl::expected<std::optional<ConfigReader::RawItem>, ConfigReader::Error>
ConfigReader::read_next_raw_item()
{
  if (m_error) {
    return tl::make_unexpected(*m_error);
  }

  if (m_current_item >= m_items.size()) {
    return std::nullopt; // EOF
  }

  return m_items[m_current_item++];
}

tl::expected<std::optional<ConfigReader::Item>, ConfigReader::Error>
ConfigReader::read_next_item()
{
  auto raw_item_result = read_next_raw_item();
  if (!raw_item_result) {
    return tl::make_unexpected(raw_item_result.error());
  }

  auto& raw_item = *raw_item_result;
  if (!raw_item) {
    return std::nullopt;
  }

  std::string_view raw_value =
    m_config.substr(raw_item->value_start_pos, raw_item->value_length);

  // Normalize the value: skip comments/blanks, join continuation lines
  std::string normalized_value;
  auto value_lines =
    split_into_views(raw_value, "\n", Tokenizer::Mode::include_empty);

  for (auto line : value_lines) {
    std::string stripped = util::strip_whitespace(line);
    if (!stripped.empty() && stripped[0] != '#') {
      if (!normalized_value.empty()) {
        normalized_value += ' ';
      }
      normalized_value += stripped;
    }
  }

  return Item{raw_item->line_number, raw_item->key, normalized_value};
}

} // namespace util
