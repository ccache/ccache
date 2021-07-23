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

#include "TextTable.hpp"

#include <third_party/fmt/core.h>

#include <algorithm>

namespace util {

void
TextTable::add_heading(const std::string& text)
{
  Cell cell(text);
  cell.m_heading = true;
  m_rows.push_back({cell});
}

void
TextTable::add_row(const std::initializer_list<Cell> cells)
{
  m_rows.emplace_back(cells);
}

std::string
TextTable::render() const
{
  std::vector<size_t> column_widths;

  for (const auto& row : m_rows) {
    column_widths.resize(std::max(column_widths.size(), row.size()));
    for (size_t i = 0; i < row.size(); ++i) {
      if (!row[i].m_heading) {
        column_widths[i] = std::max(column_widths[i], row[i].m_text.size());
      }
    }
  }

  std::string result;
  for (const auto& row : m_rows) {
    std::string r;
    for (size_t i = 0; i < row.size(); ++i) {
      if (i > 0) {
        r += ' ';
      }
      r += fmt::format((row[i].m_right_align ? "{:>{}}" : "{:<{}}"),
                       row[i].m_text,
                       column_widths[i]);
    }
    result.append(r, 0, r.find_last_not_of(' ') + 1);
    result += '\n';
  }
  return result;
}

TextTable::Cell::Cell(const std::string& text) : m_text(text)
{
}

TextTable::Cell&
TextTable::Cell::right_align()
{
  m_right_align = true;
  return *this;
}

} // namespace util
