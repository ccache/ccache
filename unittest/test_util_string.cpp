// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include <util/string.hpp>

#include <third_party/doctest.h>

#include <vector>

static bool
operator==(std::pair<std::string_view, std::optional<std::string_view>> left,
           std::pair<std::string_view, std::optional<std::string_view>> right)
{
  return left.first == right.first && left.second == right.second;
}

TEST_SUITE_BEGIN("util");

TEST_CASE("util::ends_with")
{
  CHECK(util::ends_with("", ""));
  CHECK(util::ends_with("x", ""));
  CHECK(util::ends_with("x", "x"));
  CHECK(util::ends_with("xy", ""));
  CHECK(util::ends_with("xy", "y"));
  CHECK(util::ends_with("xy", "xy"));
  CHECK(util::ends_with("xyz", ""));
  CHECK(util::ends_with("xyz", "z"));
  CHECK(util::ends_with("xyz", "yz"));
  CHECK(util::ends_with("xyz", "xyz"));

  CHECK_FALSE(util::ends_with("", "x"));
  CHECK_FALSE(util::ends_with("x", "y"));
  CHECK_FALSE(util::ends_with("x", "xy"));
}

TEST_CASE("util::join")
{
  {
    std::vector<std::string> v;
    CHECK(util::join(v, "|") == "");
  }
  {
    std::vector<std::string> v{"a"};
    CHECK(util::join(v, "|") == "a");
  }
  {
    std::vector<std::string> v{"a", " b ", "c|"};
    CHECK(util::join(v, "|") == "a| b |c|");
    CHECK(util::join(v.begin(), v.end(), "|") == "a| b |c|");
    CHECK(util::join(v.begin() + 1, v.end(), "|") == " b |c|");
  }
  {
    std::vector<std::string_view> v{"1", "2"};
    CHECK(util::join(v, " ") == "1 2");
  }
}

TEST_CASE("util::parse_double")
{
  CHECK(*util::parse_double("0") == doctest::Approx(0.0));
  CHECK(*util::parse_double(".0") == doctest::Approx(0.0));
  CHECK(*util::parse_double("0.") == doctest::Approx(0.0));
  CHECK(*util::parse_double("0.0") == doctest::Approx(0.0));
  CHECK(*util::parse_double("2.1") == doctest::Approx(2.1));
  CHECK(*util::parse_double("-42.789") == doctest::Approx(-42.789));

  CHECK(util::parse_double("").error() == "invalid floating point: \"\"");
  CHECK(util::parse_double("x").error() == "invalid floating point: \"x\"");
}

TEST_CASE("util::parse_signed")
{
  CHECK(*util::parse_signed("0") == 0);
  CHECK(*util::parse_signed("2") == 2);
  CHECK(*util::parse_signed("-17") == -17);
  CHECK(*util::parse_signed("42") == 42);
  CHECK(*util::parse_signed("0666") == 666);
  CHECK(*util::parse_signed(" 777 ") == 777);

  CHECK(util::parse_signed("").error() == "invalid integer: \"\"");
  CHECK(util::parse_signed("x").error() == "invalid integer: \"x\"");
  CHECK(util::parse_signed("0x").error() == "invalid integer: \"0x\"");
  CHECK(util::parse_signed("0x4").error() == "invalid integer: \"0x4\"");

  // Custom description not used for invalid value.
  CHECK(
    util::parse_signed("apple", std::nullopt, std::nullopt, "banana").error()
    == "invalid integer: \"apple\"");

  // Boundary values.
  CHECK(util::parse_signed("-9223372036854775809").error()
        == "invalid integer: \"-9223372036854775809\"");
  CHECK(*util::parse_signed("-9223372036854775808") == INT64_MIN);
  CHECK(*util::parse_signed("9223372036854775807") == INT64_MAX);
  CHECK(util::parse_signed("9223372036854775808").error()
        == "invalid integer: \"9223372036854775808\"");

  // Min and max values.
  CHECK(util::parse_signed("-2", -1, 1).error()
        == "integer must be between -1 and 1");
  CHECK(*util::parse_signed("-1", -1, 1) == -1);
  CHECK(*util::parse_signed("0", -1, 1) == 0);
  CHECK(*util::parse_signed("1", -1, 1) == 1);
  CHECK(util::parse_signed("2", -1, 1).error()
        == "integer must be between -1 and 1");

  // Custom description used for boundary violation.
  CHECK(util::parse_signed("0", 1, 2, "banana").error()
        == "banana must be between 1 and 2");
}

TEST_CASE("util::parse_umask")
{
  CHECK(util::parse_umask("1") == 1U);
  CHECK(util::parse_umask("002") == 002U);
  CHECK(util::parse_umask("777") == 0777U);
  CHECK(util::parse_umask("0777") == 0777U);

  CHECK(util::parse_umask("").error()
        == "invalid unsigned octal integer: \"\"");
  CHECK(util::parse_umask(" ").error()
        == "invalid unsigned octal integer: \"\"");
  CHECK(util::parse_umask("088").error()
        == "invalid unsigned octal integer: \"088\"");
}

TEST_CASE("util::parse_unsigned")
{
  CHECK(*util::parse_unsigned("0") == 0);
  CHECK(*util::parse_unsigned("2") == 2);
  CHECK(*util::parse_unsigned("42") == 42);
  CHECK(*util::parse_unsigned("0666") == 666);
  CHECK(*util::parse_unsigned(" 777 ") == 777);

  CHECK(util::parse_unsigned("").error() == "invalid unsigned integer: \"\"");
  CHECK(util::parse_unsigned("x").error() == "invalid unsigned integer: \"x\"");
  CHECK(util::parse_unsigned("0x").error()
        == "invalid unsigned integer: \"0x\"");
  CHECK(util::parse_unsigned("0x4").error()
        == "invalid unsigned integer: \"0x4\"");

  // Custom description not used for invalid value.
  CHECK(
    util::parse_unsigned("apple", std::nullopt, std::nullopt, "banana").error()
    == "invalid unsigned integer: \"apple\"");

  // Boundary values.
  CHECK(util::parse_unsigned("-1").error()
        == "invalid unsigned integer: \"-1\"");
  CHECK(*util::parse_unsigned("0") == 0);
  CHECK(*util::parse_unsigned("18446744073709551615") == UINT64_MAX);
  CHECK(util::parse_unsigned("18446744073709551616").error()
        == "invalid unsigned integer: \"18446744073709551616\"");

  // Base
  CHECK(*util::parse_unsigned("0666", std::nullopt, std::nullopt, "", 8)
        == 0666);
  CHECK(*util::parse_unsigned("0666", std::nullopt, std::nullopt, "", 10)
        == 666);
  CHECK(*util::parse_unsigned("0666", std::nullopt, std::nullopt, "", 16)
        == 0x666);
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

TEST_CASE("util::replace_all")
{
  CHECK(util::replace_all("", "", "") == "");
  CHECK(util::replace_all("x", "", "") == "x");
  CHECK(util::replace_all("", "x", "") == "");
  CHECK(util::replace_all("", "", "x") == "");
  CHECK(util::replace_all("x", "y", "z") == "x");
  CHECK(util::replace_all("x", "x", "y") == "y");
  CHECK(util::replace_all("abc", "abc", "defdef") == "defdef");
  CHECK(util::replace_all("xabc", "abc", "defdef") == "xdefdef");
  CHECK(util::replace_all("abcx", "abc", "defdef") == "defdefx");
  CHECK(util::replace_all("xabcyabcz", "abc", "defdef") == "xdefdefydefdefz");
}

TEST_CASE("util::replace_first")
{
  CHECK(util::replace_first("", "", "") == "");
  CHECK(util::replace_first("x", "", "") == "x");
  CHECK(util::replace_first("", "x", "") == "");
  CHECK(util::replace_first("", "", "x") == "");
  CHECK(util::replace_first("x", "y", "z") == "x");
  CHECK(util::replace_first("x", "x", "y") == "y");
  CHECK(util::replace_first("xabcyabcz", "abc", "defdef") == "xdefdefyabcz");
}

TEST_CASE("util::split_once")
{
  using std::make_pair;
  using std::nullopt;
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

TEST_CASE("util::starts_with")
{
  // starts_with(const char*, string_view)
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

  // starts_with(string_view, string_view)
  CHECK(util::starts_with(std::string(""), ""));
  CHECK(util::starts_with(std::string("x"), ""));
  CHECK(util::starts_with(std::string("x"), "x"));
  CHECK(util::starts_with(std::string("xy"), ""));
  CHECK(util::starts_with(std::string("xy"), "x"));
  CHECK(util::starts_with(std::string("xy"), "xy"));
  CHECK(util::starts_with(std::string("xyz"), ""));
  CHECK(util::starts_with(std::string("xyz"), "x"));
  CHECK(util::starts_with(std::string("xyz"), "xy"));
  CHECK(util::starts_with(std::string("xyz"), "xyz"));

  CHECK_FALSE(util::starts_with(std::string(""), "x"));
  CHECK_FALSE(util::starts_with(std::string("x"), "y"));
  CHECK_FALSE(util::starts_with(std::string("x"), "xy"));
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

TEST_SUITE_END();
