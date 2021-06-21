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

#include <util/string_utils.hpp>

#include <third_party/doctest.h>

static bool
operator==(
  std::pair<nonstd::string_view, nonstd::optional<nonstd::string_view>> left,
  std::pair<nonstd::string_view, nonstd::optional<nonstd::string_view>> right)
{
  return left.first == right.first && left.second == right.second;
}

TEST_CASE("util::percent_decode")
{
  CHECK(util::percent_decode("") == "");
  CHECK(util::percent_decode("a") == "a");
  CHECK(util::percent_decode("%61") == "a");
  CHECK(util::percent_decode("%ab") == "\xab");
  CHECK(util::percent_decode("%aB") == "\xab");
  CHECK(util::percent_decode("%Ab") == "\xab");
  CHECK(util::percent_decode("%AB") == "\xab");
  CHECK(util::percent_decode("a%25b%7cc") == "a%b|c");

  CHECK(util::percent_decode("%").error()
        == "invalid percent-encoded string at position 0: %");
  CHECK(util::percent_decode("%6").error()
        == "invalid percent-encoded string at position 0: %6");
  CHECK(util::percent_decode("%%").error()
        == "invalid percent-encoded string at position 0: %%");
  CHECK(util::percent_decode("a%0g").error()
        == "invalid percent-encoded string at position 1: a%0g");
}

TEST_CASE("util::split_once")
{
  using nonstd::nullopt;
  using std::make_pair;
  using util::split_once;

  CHECK(split_once("", '=') == make_pair("", nullopt));
  CHECK(split_once("a", '=') == make_pair("a", nullopt));
  CHECK(split_once("=a", '=') == make_pair("", "a"));
  CHECK(split_once("a=", '=') == make_pair("a", ""));
  CHECK(split_once("a==", '=') == make_pair("a", "="));
  CHECK(split_once("a=b", '=') == make_pair("a", "b"));
  CHECK(split_once("a=b=", '=') == make_pair("a", "b="));
  CHECK(split_once("a=b=c", '=') == make_pair("a", "b=c"));
  CHECK(split_once("x y", ' ') == make_pair("x", "y"));
}
