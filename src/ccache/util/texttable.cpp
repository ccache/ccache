// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "texttable.hpp"

#include <ccache/util/assertions.hpp>
#include <ccache/util/format.hpp>

#include <fmt/core.h>

#include <algorithm>

namespace util {

void
TextTable::add_heading(const std::string& text)
{
  Cell cell(text);
  cell.m_heading = true;
  m_rows.push_back({cell});
  m_columns = std::max(m_columns, size_t{1});
}

void
TextTable::add_row(const std::vector<Cell>& cells)
{
  m_rows.emplace_back();
  for (const auto& cell : cells) {
    for (size_t i = 0; i < cell.m_colspan - 1; ++i) {
      Cell dummy("");
      dummy.m_colspan = 0;
      m_rows.back().push_back(dummy);
    }
    m_rows.back().push_back(cell);

    m_columns = std::max(m_columns, m_rows.back().size());
  }
}

std::vector<size_t>
TextTable::compute_column_widths() const
{
  std::vector<size_t> result(m_columns, 0);

  for (size_t column_index = 0; column_index < m_columns; ++column_index) {
    for (const auto& row : m_rows) {
      if (column_index >= row.size()) {
        continue;
      }
      const auto& cell = row[column_index];
      if (cell.m_heading || cell.m_colspan == 0) {
        continue;
      }

      size_t width_of_left_cols_in_span = 0;
      for (size_t i = 0; i < cell.m_colspan - 1; ++i) {
        width_of_left_cols_in_span += 1 + result[column_index - i - 1];
      }
      result[column_index] = std::max(
        result[column_index],
        cell.m_text.length()
          - std::min(width_of_left_cols_in_span, cell.m_text.length()));
    }
  }

  return result;
}

std::string
TextTable::render() const
{
  auto column_widths = compute_column_widths();

  std::string result;
  for (const auto& row : m_rows) {
    ASSERT(column_widths.size() >= row.size());

    std::string r;
    bool first = true;
    for (size_t i = 0; i < row.size(); ++i) {
      const auto& cell = row[i];
      if (cell.m_colspan == 0) {
        continue;
      }
      if (first) {
        first = false;
      } else {
        r += ' ';
      }

      size_t width = 0;
      for (size_t j = i + 1 - cell.m_colspan; j <= i; ++j) {
        width += column_widths[j] + (j == i ? 0 : 1);
      }
      r += cell.m_right_align ? FMT("{:>{}}", cell.m_text, width)
                              : FMT("{:<{}}", cell.m_text, width);
    }
    result.append(r, 0, r.find_last_not_of(' ') + 1);
    result += '\n';
  }
  return result;
}

TextTable::Cell::Cell(const std::string& text)
  : m_text(text)
{
}

TextTable::Cell::Cell(std::string_view text)
  : Cell(std::string(text))
{
}

TextTable::Cell::Cell(const char* text)
  : Cell(std::string(text))
{
}

TextTable::Cell::Cell(const uint64_t number)
  : m_text(fmt::format("{}", number)),
    m_right_align(true)
{
}

TextTable::Cell&
TextTable::Cell::left_align()
{
  m_right_align = false;
  return *this;
}

TextTable::Cell&
TextTable::Cell::colspan(const size_t columns)
{
  ASSERT(columns >= 1);
  m_colspan = columns;
  return *this;
}

TextTable::Cell&
TextTable::Cell::right_align()
{
  m_right_align = true;
  return *this;
}

} // namespace util
