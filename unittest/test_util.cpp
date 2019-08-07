// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "../src/util.hpp"

#include "third_party/catch.hpp"

TEST_CASE("util::read_file and util::write_file")
{
  util::write_file("test", "foo\nbar\n");
  std::string data = util::read_file("test");
  CHECK(data == "foo\nbar\n");
}

TEST_CASE("util::starts_with")
{
  CHECK(util::starts_with("", ""));
  CHECK(util::starts_with("x", ""));
  CHECK(util::starts_with("x", "x"));
  CHECK(util::starts_with("xy", ""));
  CHECK(util::starts_with("xy", "x"));
  CHECK(util::starts_with("xy", "xy"));
  CHECK(util::starts_with("xyz", ""));
  CHECK(util::starts_with("xyz", "x"));
  CHECK(util::starts_with("xyz", "xy"));
  CHECK(util::starts_with("xyz", "xyz"));

  CHECK_FALSE(util::starts_with("", "x"));
  CHECK_FALSE(util::starts_with("x", "y"));
  CHECK_FALSE(util::starts_with("x", "xy"));
}

TEST_CASE("util::strip_whitespace")
{
  CHECK(util::strip_whitespace("") == "");
  CHECK(util::strip_whitespace("x") == "x");
  CHECK(util::strip_whitespace(" x") == "x");
  CHECK(util::strip_whitespace("x ") == "x");
  CHECK(util::strip_whitespace(" x ") == "x");
  CHECK(util::strip_whitespace(" \n\tx \n\t") == "x");
  CHECK(util::strip_whitespace("  x  y  ") == "x  y");
}

TEST_CASE("util::to_lowercase")
{
  CHECK(util::to_lowercase("") == "");
  CHECK(util::to_lowercase("x") == "x");
  CHECK(util::to_lowercase("X") == "x");
  CHECK(util::to_lowercase(" x_X@") == " x_x@");
}
