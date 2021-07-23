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

#include <util/TextTable.hpp>

#include <third_party/doctest.h>

#include <iostream> // macOS bug: https://github.com/onqtam/doctest/issues/126

TEST_CASE("TextTable")
{
  using C = util::TextTable::Cell;

  util::TextTable table;

  SUBCASE("empty")
  {
    CHECK(table.render() == "");
  }

  SUBCASE("1x1")
  {
    table.add_row({{"a"}});
    CHECK(table.render() == "a\n");
  }

  SUBCASE("2x1 with space prefix/suffix")
  {
    table.add_row({{" a "}, C(" b ")});
    CHECK(table.render() == " a   b\n");
  }

  SUBCASE("1x2")
  {
    table.add_row({{"a"}});
    table.add_row({{"b"}});
    CHECK(table.render() == "a\nb\n");
  }

  SUBCASE("3 + 2")
  {
    table.add_row({{"a"}, {"b"}, {"c"}});
    table.add_row({{"aa"}, {"bbb"}});
    CHECK(table.render()
          == ("a  b   c\n"
              "aa bbb\n"));
  }

  SUBCASE("right align")
  {
    table.add_row({{"a"}, {"bbb"}, {"cc"}});
    table.add_row(
      {C("aa").right_align(), C("b").right_align(), C("ccc").right_align()});
    table.add_row({{"aaa"}, {"bb"}, {"c"}});
    CHECK(table.render()
          == ("a   bbb cc\n"
              " aa   b ccc\n"
              "aaa bb  c\n"));
  }

  SUBCASE("heading")
  {
    table.add_row({{"a"}, {"b"}, {"c"}});
    table.add_heading("DDDDDD");
    table.add_row({{"aaa"}, {"bbb"}, {"ccc"}});
    CHECK(table.render()
          == ("a   b   c\n"
              "DDDDDD\n"
              "aaa bbb ccc\n"));
  }
}
