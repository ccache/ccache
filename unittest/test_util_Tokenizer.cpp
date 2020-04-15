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

#include "../src/Util.hpp"

#include "third_party/doctest.h"

TEST_CASE("util::Tokenizer")
{
  CHECK(Util::split_into_views("", "/").empty());
  CHECK(Util::split_into_views("///", "/").empty());
  {
    const auto s = Util::split_into_views("a/b", "/");
    REQUIRE(s.size() == 2);
    CHECK(s[0] == "a");
    CHECK(s[1] == "b");
  }
  {
    const auto s = Util::split_into_views("a/b", "x");
    REQUIRE(s.size() == 1);
    CHECK(s[0] == "a/b");
  }
  {
    const auto s = Util::split_into_views("a/b:c", "/:");
    REQUIRE(s.size() == 3);
    CHECK(s[0] == "a");
    CHECK(s[1] == "b");
    CHECK(s[2] == "c");
  }
  {
    const auto s = Util::split_into_views(":a//b..:.c/:/.", "/:.");
    REQUIRE(s.size() == 3);
    CHECK(s[0] == "a");
    CHECK(s[1] == "b");
    CHECK(s[2] == "c");
  }
  {
    const auto s =
      Util::split_into_views(".0.1.2.3.4.5.6.7.8.9.", "/:.+_abcdef");
    REQUIRE(s.size() == 10);
    CHECK(s[0] == "0");
    CHECK(s[9] == "9");
  }
}
