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

#include <ccache/util/texttable.hpp>

#include <doctest/doctest.h>

#include <iostream> // macOS bug: https://github.com/onqtam/doctest/issues/126

TEST_CASE("TextTable")
{
  using C = util::TextTable::Cell;

  util::TextTable table;

  SUBCASE("empty")
  {
    CHECK(table.render() == "");
  }

  SUBCASE("only a heading")
  {
    table.add_heading("heading");
    CHECK(table.render() == "heading\n");
  }

  SUBCASE("1x1")
  {
    table.add_row({"a"});
    CHECK(table.render() == "a\n");
  }

  SUBCASE("2x1 with space prefix/suffix")
  {
    table.add_row({std::string(" a "), C(" b ")});
    CHECK(table.render() == " a   b\n");
  }

  SUBCASE("1x2")
  {
    table.add_row({"a"});
    table.add_row({1});
    CHECK(table.render() == "a\n1\n");
  }

  SUBCASE("3 + 2")
  {
    table.add_row({"a", "b", "c"});
    table.add_row({"aa", "bbb"});
    CHECK(table.render()
          == ("a  b   c\n"
              "aa bbb\n"));
  }

  SUBCASE("strings and numbers")
  {
    table.add_row({"a", 123, "cc"});
    table.add_row({"aa", 4, "ccc"});
    table.add_row({"aaa", 56, "c"});
    CHECK(table.render()
          == ("a   123 cc\n"
              "aa    4 ccc\n"
              "aaa  56 c\n"));
  }

  SUBCASE("left align")
  {
    table.add_row({"a", 123, "cc"});
    table.add_row({"aa", C(4).left_align(), "ccc"});
    table.add_row({"aaa", 56, "c"});
    CHECK(table.render()
          == ("a   123 cc\n"
              "aa  4   ccc\n"
              "aaa  56 c\n"));
  }

  SUBCASE("right align")
  {
    table.add_row({"a", "bbb", "cc"});
    table.add_row(
      {C("aa").right_align(), C("b").right_align(), C("ccc").right_align()});
    table.add_row({"aaa", "bb", "c"});
    CHECK(table.render()
          == ("a   bbb cc\n"
              " aa   b ccc\n"
              "aaa bb  c\n"));
  }

  SUBCASE("heading")
  {
    table.add_row({"a", "b", "c"});
    table.add_heading("DDDDDD");
    table.add_row({"aaa", "bbb", "ccc"});
    CHECK(table.render()
          == ("a   b   c\n"
              "DDDDDD\n"
              "aaa bbb ccc\n"));
  }

  SUBCASE("colspan")
  {
    table.add_row({C("22").colspan(2), C("2r").colspan(2).right_align()});
    table.add_row({C("1").colspan(1), C("22222").colspan(2), "1"});
    table.add_row({"1", "1", "1", "1", "1"});
    table.add_row({"1", C("3333333333").colspan(3), "1"});
    CHECK(table.render()
          == ("22        2r\n"      // 4 columns
              "1 22222 1\n"         // 4 columns
              "1 1 1   1    1\n"    // 5 columns
              "1 3333333333 1\n")); // 5 columns
  }
}
