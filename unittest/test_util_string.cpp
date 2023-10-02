// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <vector>

static bool
operator==(std::pair<std::string, std::optional<std::string>> left,
           std::pair<std::string, std::optional<std::string>> right)
{
  return left.first == right.first && left.second == right.second;
}

static bool
operator==(std::pair<std::string_view, std::optional<std::string_view>> left,
           std::pair<std::string_view, std::optional<std::string_view>> right)
{
  return left.first == right.first && left.second == right.second;
}

TEST_SUITE_BEGIN("util");

TEST_CASE("util::format_argv_as_win32_command_string")
{
  {
    const char* const argv[] = {"a", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "") == R"("a")");
  }
  {
    const char* const argv[] = {"a", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "p") == R"("p" "a")");
  }
  {
    const char* const argv[] = {"a", "b c", "\"d\"", "'e'", "\\\"h", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "")
          == R"("a" "b c" "\"d\"" "'e'" "\\\"h")");
  }
  {
    const char* const argv[] = {"a\\b\\c", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "") == R"("a\b\c")");
  }
  {
    const char* const argv[] = {"a\\b\\c", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "", true)
          == R"("a\\b\\c")");
  }
  {
    const char* const argv[] = {R"(a\b \"c\" \)", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "")
          == R"("a\b \\\"c\\\" \\")");
  }
  {
    const char* const argv[] = {R"(a\b \"c\" \)", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, "", true)
          == R"("a\\b \\\"c\\\" \\")");
  }
}

TEST_CASE("util::format_argv_for_logging")
{
  SUBCASE("nullptr")
  {
    const char* argv[] = {nullptr};
    CHECK(util::format_argv_for_logging(argv) == "");
  }

  SUBCASE("plain arguments")
  {
    const char* argv[] = {"foo", "bar", nullptr};
    CHECK(util::format_argv_for_logging(argv) == "foo bar");
  }

  SUBCASE("argument with space")
  {
    const char* argv[] = {"foo bar", "fum", nullptr};
    CHECK(util::format_argv_for_logging(argv) == "\"foo bar\" fum");
  }

  SUBCASE("argument with double quote")
  {
    const char* argv[] = {"foo\"bar", "fum", nullptr};
    CHECK(util::format_argv_for_logging(argv) == "foo\\\"bar fum");
  }

  SUBCASE("argument with backslash")
  {
    const char* argv[] = {"foo\\bar", "fum", nullptr};
    CHECK(util::format_argv_for_logging(argv) == "foo\\\\bar fum");
  }
}

TEST_CASE("util::format_base16")
{
  uint8_t none[] = "";
  uint8_t text[4] = "foo"; // incl. NUL
  uint8_t data[4] = {0, 1, 2, 3};

  CHECK(util::format_base16({none, 0}) == "");
  CHECK(util::format_base16({text, sizeof(text)}) == "666f6f00");
  CHECK(util::format_base16({data, sizeof(data)}) == "00010203");
}

TEST_CASE("util::format_base32hex")
{
  // Test vectors (without padding) from RFC 4648.
  const uint8_t input[] = {'f', 'o', 'o', 'b', 'a', 'r'};
  CHECK(util::format_base32hex({input, 0}) == "");
  CHECK(util::format_base32hex({input, 1}) == "co");
  CHECK(util::format_base32hex({input, 2}) == "cpng");
  CHECK(util::format_base32hex({input, 3}) == "cpnmu");
  CHECK(util::format_base32hex({input, 4}) == "cpnmuog");
  CHECK(util::format_base32hex({input, 5}) == "cpnmuoj1");
  CHECK(util::format_base32hex({input, 6}) == "cpnmuoj1e8");
}

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

TEST_CASE("util::format_human_readable_diff")
{
  using SUPT = util::SizeUnitPrefixType;

  SUBCASE("binary")
  {
    CHECK(util::format_human_readable_diff(0, SUPT::binary) == "0 bytes");
    CHECK(util::format_human_readable_diff(1, SUPT::binary) == "+1 byte");
    CHECK(util::format_human_readable_diff(42, SUPT::binary) == "+42 bytes");
    CHECK(util::format_human_readable_diff(1949, SUPT::binary) == "+1.9 KiB");
    CHECK(util::format_human_readable_diff(1951, SUPT::binary) == "+1.9 KiB");
    CHECK(util::format_human_readable_diff(499.7 * 1000, SUPT::binary)
          == "+488.0 KiB");
    CHECK(util::format_human_readable_diff(1000 * 1000, SUPT::binary)
          == "+976.6 KiB");
    CHECK(util::format_human_readable_diff(1234 * 1000, SUPT::binary)
          == "+1.2 MiB");
    CHECK(util::format_human_readable_diff(438.5 * 1000 * 1000, SUPT::binary)
          == "+418.2 MiB");
    CHECK(util::format_human_readable_diff(1000 * 1000 * 1000, SUPT::binary)
          == "+953.7 MiB");
    CHECK(
      util::format_human_readable_diff(17.11 * 1000 * 1000 * 1000, SUPT::binary)
      == "+15.9 GiB");

    CHECK(util::format_human_readable_diff(-1, SUPT::binary) == "-1 byte");
    CHECK(util::format_human_readable_diff(-42, SUPT::binary) == "-42 bytes");
    CHECK(util::format_human_readable_diff(-1949, SUPT::binary) == "-1.9 KiB");
    CHECK(util::format_human_readable_diff(-1951, SUPT::binary) == "-1.9 KiB");
    CHECK(util::format_human_readable_diff(-499.7 * 1000, SUPT::binary)
          == "-488.0 KiB");
    CHECK(util::format_human_readable_diff(-1000 * 1000, SUPT::binary)
          == "-976.6 KiB");
    CHECK(util::format_human_readable_diff(-1234 * 1000, SUPT::binary)
          == "-1.2 MiB");
    CHECK(util::format_human_readable_diff(-438.5 * 1000 * 1000, SUPT::binary)
          == "-418.2 MiB");
    CHECK(util::format_human_readable_diff(-1000 * 1000 * 1000, SUPT::binary)
          == "-953.7 MiB");
    CHECK(util::format_human_readable_diff(-17.11 * 1000 * 1000 * 1000,
                                           SUPT::binary)
          == "-15.9 GiB");
  }

  SUBCASE("decimal")
  {
    CHECK(util::format_human_readable_diff(0, SUPT::decimal) == "0 bytes");
    CHECK(util::format_human_readable_diff(1, SUPT::decimal) == "+1 byte");
    CHECK(util::format_human_readable_diff(42, SUPT::decimal) == "+42 bytes");
    CHECK(util::format_human_readable_diff(1949, SUPT::decimal) == "+1.9 kB");
    CHECK(util::format_human_readable_diff(1951, SUPT::decimal) == "+2.0 kB");
    CHECK(util::format_human_readable_diff(499.7 * 1000, SUPT::decimal)
          == "+499.7 kB");
    CHECK(util::format_human_readable_diff(1000 * 1000, SUPT::decimal)
          == "+1.0 MB");
    CHECK(util::format_human_readable_diff(1234 * 1000, SUPT::decimal)
          == "+1.2 MB");
    CHECK(util::format_human_readable_diff(438.5 * 1000 * 1000, SUPT::decimal)
          == "+438.5 MB");
    CHECK(util::format_human_readable_diff(1000 * 1000 * 1000, SUPT::decimal)
          == "+1.0 GB");
    CHECK(util::format_human_readable_diff(17.11 * 1000 * 1000 * 1000,
                                           SUPT::decimal)
          == "+17.1 GB");

    CHECK(util::format_human_readable_diff(-1, SUPT::decimal) == "-1 byte");
    CHECK(util::format_human_readable_diff(-42, SUPT::decimal) == "-42 bytes");
    CHECK(util::format_human_readable_diff(-1949, SUPT::decimal) == "-1.9 kB");
    CHECK(util::format_human_readable_diff(-1951, SUPT::decimal) == "-2.0 kB");
    CHECK(util::format_human_readable_diff(-499.7 * 1000, SUPT::decimal)
          == "-499.7 kB");
    CHECK(util::format_human_readable_diff(-1000 * 1000, SUPT::decimal)
          == "-1.0 MB");
    CHECK(util::format_human_readable_diff(-1234 * 1000, SUPT::decimal)
          == "-1.2 MB");
    CHECK(util::format_human_readable_diff(-438.5 * 1000 * 1000, SUPT::decimal)
          == "-438.5 MB");
    CHECK(util::format_human_readable_diff(-1000 * 1000 * 1000, SUPT::decimal)
          == "-1.0 GB");
    CHECK(util::format_human_readable_diff(-17.11 * 1000 * 1000 * 1000,
                                           SUPT::decimal)
          == "-17.1 GB");
  }
}

TEST_CASE("util::format_human_readable_size")
{
  using SUPT = util::SizeUnitPrefixType;

  SUBCASE("binary")
  {
    CHECK(util::format_human_readable_size(0, SUPT::binary) == "0 bytes");
    CHECK(util::format_human_readable_size(1, SUPT::binary) == "1 byte");
    CHECK(util::format_human_readable_size(42, SUPT::binary) == "42 bytes");
    CHECK(util::format_human_readable_size(1949, SUPT::binary) == "1.9 KiB");
    CHECK(util::format_human_readable_size(1951, SUPT::binary) == "1.9 KiB");
    CHECK(util::format_human_readable_size(499.7 * 1000, SUPT::binary)
          == "488.0 KiB");
    CHECK(util::format_human_readable_size(1000 * 1000, SUPT::binary)
          == "976.6 KiB");
    CHECK(util::format_human_readable_size(1234 * 1000, SUPT::binary)
          == "1.2 MiB");
    CHECK(util::format_human_readable_size(438.5 * 1000 * 1000, SUPT::binary)
          == "418.2 MiB");
    CHECK(util::format_human_readable_size(1000 * 1000 * 1000, SUPT::binary)
          == "953.7 MiB");
    CHECK(
      util::format_human_readable_size(17.11 * 1000 * 1000 * 1000, SUPT::binary)
      == "15.9 GiB");
  }

  SUBCASE("decimal")
  {
    CHECK(util::format_human_readable_size(0, SUPT::decimal) == "0 bytes");
    CHECK(util::format_human_readable_size(1, SUPT::decimal) == "1 byte");
    CHECK(util::format_human_readable_size(42, SUPT::decimal) == "42 bytes");
    CHECK(util::format_human_readable_size(1949, SUPT::decimal) == "1.9 kB");
    CHECK(util::format_human_readable_size(1951, SUPT::decimal) == "2.0 kB");
    CHECK(util::format_human_readable_size(499.7 * 1000, SUPT::decimal)
          == "499.7 kB");
    CHECK(util::format_human_readable_size(1000 * 1000, SUPT::decimal)
          == "1.0 MB");
    CHECK(util::format_human_readable_size(1234 * 1000, SUPT::decimal)
          == "1.2 MB");
    CHECK(util::format_human_readable_size(438.5 * 1000 * 1000, SUPT::decimal)
          == "438.5 MB");
    CHECK(util::format_human_readable_size(1000 * 1000 * 1000, SUPT::decimal)
          == "1.0 GB");
    CHECK(util::format_human_readable_size(17.11 * 1000 * 1000 * 1000,
                                           SUPT::decimal)
          == "17.1 GB");
  }
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

TEST_CASE("util::parse_duration")
{
  CHECK(*util::parse_duration("0s") == 0);
  CHECK(*util::parse_duration("2s") == 2);
  CHECK(*util::parse_duration("1d") == 3600 * 24);
  CHECK(*util::parse_duration("2d") == 2 * 3600 * 24);
  CHECK(util::parse_duration("-2").error()
        == "invalid suffix (supported: d (day) and s (second)): \"-2\"");
  CHECK(util::parse_duration("2x").error()
        == "invalid suffix (supported: d (day) and s (second)): \"2x\"");
  CHECK(util::parse_duration("2").error()
        == "invalid suffix (supported: d (day) and s (second)): \"2\"");
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

TEST_CASE("util::parse_size")
{
  using SUPT = util::SizeUnitPrefixType;

  auto u64 = [](auto i) { return static_cast<uint64_t>(i); };
  auto h = [&](auto size, auto st) { return std::make_pair(u64(size), st); };

  // Default suffix: Gi
  CHECK(*util::parse_size("0") == h(0, SUPT::binary));
  CHECK(*util::parse_size("42")
        == h(u64(42) * 1024 * 1024 * 1024, SUPT::binary));

  // Decimal suffixes
  CHECK(*util::parse_size("78k") == h(78 * 1000, SUPT::decimal));
  CHECK(*util::parse_size("78K") == h(78 * 1000, SUPT::decimal));
  CHECK(*util::parse_size("1.1 M") == h(u64(1.1 * 1000 * 1000), SUPT::decimal));
  CHECK(*util::parse_size("438.55M")
        == h(u64(438.55 * 1000 * 1000), SUPT::decimal));
  CHECK(*util::parse_size("1 G") == h(1 * 1000 * 1000 * 1000, SUPT::decimal));
  CHECK(*util::parse_size("2T")
        == h(u64(2) * 1000 * 1000 * 1000 * 1000, SUPT::decimal));

  // Binary suffixes
  CHECK(*util::parse_size("78 Ki") == h(78 * 1024, SUPT::binary));
  CHECK(*util::parse_size("1.1Mi") == h(u64(1.1 * 1024 * 1024), SUPT::binary));
  CHECK(*util::parse_size("438.55 Mi")
        == h(u64(438.55 * 1024 * 1024), SUPT::binary));
  CHECK(*util::parse_size("1Gi") == h(1 * 1024 * 1024 * 1024, SUPT::binary));
  CHECK(*util::parse_size("2 Ti")
        == h(u64(2) * 1024 * 1024 * 1024 * 1024, SUPT::binary));

  // With B suffix
  CHECK(*util::parse_size("9MB") == h(9 * 1000 * 1000, SUPT::decimal));
  CHECK(*util::parse_size("9MiB") == h(9 * 1024 * 1024, SUPT::binary));

  // Errors
  CHECK(util::parse_size("").error() == "invalid size: \"\"");
  CHECK(util::parse_size("x").error() == "invalid size: \"x\"");
  CHECK(util::parse_size("10x").error() == "invalid size: \"10x\"");
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

TEST_CASE("util::split_into_strings")
{
  // Edge cases are tested in test_util_Tokenizer.cpp.
  CHECK(util::split_into_strings("foo bar", " ")
        == std::vector<std::string>{"foo", "bar"});
}

TEST_CASE("util::split_into_views")
{
  // Edge cases are tested in test_util_Tokenizer.cpp.
  CHECK(util::split_into_views("foo bar", " ")
        == std::vector<std::string_view>{"foo", "bar"});
}

TEST_CASE("util::split_once")
{
  using std::make_pair;
  using std::nullopt;
  using util::split_once;

  SUBCASE("const char*")
  {
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

  SUBCASE("std::string&&")
  {
    CHECK(split_once(std::string(""), '=') == make_pair("", nullopt));
    CHECK(split_once(std::string("a"), '=') == make_pair("a", nullopt));
    CHECK(split_once(std::string("=a"), '=') == make_pair("", "a"));
    CHECK(split_once(std::string("a="), '=') == make_pair("a", ""));
    CHECK(split_once(std::string("a=="), '=') == make_pair("a", "="));
    CHECK(split_once(std::string("a=b"), '=') == make_pair("a", "b"));
    CHECK(split_once(std::string("a=b="), '=') == make_pair("a", "b="));
    CHECK(split_once(std::string("a=b=c"), '=') == make_pair("a", "b=c"));
    CHECK(split_once(std::string("x y"), ' ') == make_pair("x", "y"));
  }

  SUBCASE("std::string_view")
  {
    CHECK(split_once(std::string_view(""), '=') == make_pair("", nullopt));
    CHECK(split_once(std::string_view("a"), '=') == make_pair("a", nullopt));
    CHECK(split_once(std::string_view("=a"), '=') == make_pair("", "a"));
    CHECK(split_once(std::string_view("a="), '=') == make_pair("a", ""));
    CHECK(split_once(std::string_view("a=="), '=') == make_pair("a", "="));
    CHECK(split_once(std::string_view("a=b"), '=') == make_pair("a", "b"));
    CHECK(split_once(std::string_view("a=b="), '=') == make_pair("a", "b="));
    CHECK(split_once(std::string_view("a=b=c"), '=') == make_pair("a", "b=c"));
    CHECK(split_once(std::string_view("x y"), ' ') == make_pair("x", "y"));
  }
}

TEST_CASE("util::split_path_list")
{
  CHECK(util::split_path_list("").empty());
  {
    const auto v = util::split_path_list("a");
    REQUIRE(v.size() == 1);
    CHECK(v[0] == "a");
  }
  {
    const auto v = util::split_path_list("a/b");
    REQUIRE(v.size() == 1);
    CHECK(v[0] == "a/b");
  }
  {
#ifdef _WIN32
    const auto v = util::split_path_list("a/b;c");
#else
    const auto v = util::split_path_list("a/b:c");
#endif
    REQUIRE(v.size() == 2);
    CHECK(v[0] == "a/b");
    CHECK(v[1] == "c");
  }
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

TEST_CASE("util::to_lowercase")
{
  CHECK(util::to_lowercase("") == "");
  CHECK(util::to_lowercase("x") == "x");
  CHECK(util::to_lowercase("X") == "x");
  CHECK(util::to_lowercase(" x_X@") == " x_x@");
}

TEST_SUITE_END();
