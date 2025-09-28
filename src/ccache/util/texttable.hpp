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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace util {

class TextTable
{
public:
  class Cell
  {
  public:
    Cell(const std::string& text);
    Cell(std::string_view text);
    Cell(const char* text);
    Cell(uint64_t number);

    Cell& colspan(size_t columns);
    Cell& left_align();
    Cell& right_align();

  private:
    friend TextTable;

    const std::string m_text;
    bool m_right_align = false;
    bool m_heading = false;
    size_t m_colspan = 1;
  };

  void add_heading(const std::string& text);
  void add_row(const std::vector<Cell>& cells);
  std::string render() const;

private:
  std::vector<std::vector<Cell>> m_rows;
  size_t m_columns = 0;

  std::vector<size_t> compute_column_widths() const;
};

} // namespace util
