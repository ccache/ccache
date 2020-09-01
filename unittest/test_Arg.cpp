// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "../src/Arg.hpp"

#include "third_party/doctest.h"
#include "third_party/fmt/core.h"

TEST_SUITE_BEGIN("Arg");

struct Split
{
  nonstd::string_view key, value;
};
static bool
operator==(const Arg& lhs, const Split& rhs)
{
  return lhs.has_been_split() == true && lhs.key() == rhs.key
         && lhs.value() == rhs.value
         && lhs.full()
              == fmt::format(
                "{}={}", std::string(rhs.key), std::string(rhs.value));
}

struct NotSplit
{
  nonstd::string_view full;
};
static bool
operator==(const Arg& lhs, const NotSplit& rhs)
{
  return lhs.has_been_split() == false && lhs.key() == "" && lhs.value() == ""
         && lhs.full() == rhs.full;
}

TEST_CASE("Arg")
{
  CHECK(Arg("") == NotSplit{""});
  CHECK(Arg("=") == Split{"", ""});
  CHECK(Arg("x") == NotSplit{"x"});
  CHECK(Arg("xy") == NotSplit{"xy"});
  CHECK(Arg("xy=") == Split{"xy", ""});
  CHECK(Arg("=xy") == Split{"", "xy"});
  CHECK(Arg("x=y") == Split{"x", "y"});
  CHECK(Arg(" x  =   y    ") == Split{" x  ", "   y    "});
  CHECK(Arg("a very very long string=another very very long string")
        == Split{"a very very long string", "another very very long string"});

  CHECK(!Arg("").has_been_split());
  CHECK(Arg("=").has_been_split());
  CHECK(Arg("x=").has_been_split());
  CHECK(Arg("=y").has_been_split());
  CHECK(Arg("x=y").has_been_split());
}

TEST_SUITE_END();
