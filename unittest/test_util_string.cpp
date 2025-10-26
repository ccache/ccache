// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/bytes.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <vector>

using namespace std::chrono_literals;

TEST_SUITE_BEGIN("util");

TEST_CASE("util::format_argv_as_win32_command_string")
{
  {
    const char* const argv[] = {"a", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv) == R"("a")");
  }
  {
    const char* const argv[] = {"a", "b c", "\"d\"", "'e'", "\\\"h", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv)
          == R"("a" "b c" "\"d\"" "'e'" "\\\"h")");
  }
  {
    const char* const argv[] = {"a\\b\\c", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv) == R"("a\b\c")");
  }
  {
    const char* const argv[] = {"a\\b\\c", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, true)
          == R"("a\\b\\c")");
  }
  {
    const char* const argv[] = {R"(a\b \"c\" \)", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv)
          == R"("a\b \\\"c\\\" \\")");
  }
  {
    const char* const argv[] = {R"(a\b \"c\" \)", nullptr};
    CHECK(util::format_argv_as_win32_command_string(argv, true)
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

TEST_CASE("util::format_duration")
{
  CHECK(util::format_duration(0ms) == "0s");
  CHECK(util::format_duration(1ms) == "1ms");
  CHECK(util::format_duration(999ms) == "999ms");
  CHECK(util::format_duration(1000ms) == "1s");
  CHECK(util::format_duration(1002ms) == "1002ms");
  CHECK(util::format_duration(7000ms) == "7s");

  CHECK(util::format_duration(0s) == "0s");
  CHECK(util::format_duration(5s) == "5s");
  CHECK(util::format_duration(59s) == "59s");
  CHECK(util::format_duration(60s) == "1m");
  CHECK(util::format_duration(61s) == "61s");
  CHECK(util::format_duration(70s) == "70s");
  CHECK(util::format_duration(119s) == "119s");
  CHECK(util::format_duration(120s) == "2m");
  CHECK(util::format_duration(3599s) == "3599s");
  CHECK(util::format_duration(3600s) == "1h");
  CHECK(util::format_duration(3601s) == "3601s");
  CHECK(util::format_duration(5000s) == "5000s");
  CHECK(util::format_duration(7199s) == "7199s");
  CHECK(util::format_duration(7200s) == "2h");
  CHECK(util::format_duration(86399s) == "86399s");
  CHECK(util::format_duration(86400s) == "1d");
  CHECK(util::format_duration(86401s) == "86401s");
  CHECK(util::format_duration(172800s) == "2d");

  CHECK(util::format_duration(4min) == "4m");
  CHECK(util::format_duration(60min) == "1h");
  CHECK(util::format_duration(90min) == "90m");

  CHECK(util::format_duration(9h) == "9h");
  CHECK(util::format_duration(48h) == "2d");
  CHECK(util::format_duration(49h) == "49h");

  auto test_round_trip = [](std::string_view str) {
    auto parsed = util::parse_duration(str);
    REQUIRE(parsed);
    CHECK(util::format_duration(*parsed) == str);
  };

  test_round_trip("0s");
  test_round_trip("1ms");
  test_round_trip("500ms");
  test_round_trip("1s");
  test_round_trip("5s");
  test_round_trip("1m");
  test_round_trip("30m");
  test_round_trip("1h");
  test_round_trip("12h");
  test_round_trip("1d");
  test_round_trip("7d");
}

TEST_CASE("util::parse_base16")
{
  SUBCASE("empty string")
  {
    auto result = util::parse_base16("");
    REQUIRE(result);
    CHECK(result->empty());
  }

  SUBCASE("valid hex strings")
  {
    auto result1 = util::parse_base16("666f6f00");
    REQUIRE(result1);
    CHECK(result1->size() == 4);
    CHECK(result1->at(0) == 0x66);
    CHECK(result1->at(1) == 0x6f);
    CHECK(result1->at(2) == 0x6f);
    CHECK(result1->at(3) == 0x00);

    auto result2 = util::parse_base16("00010203");
    REQUIRE(result2);
    CHECK(result2->size() == 4);
    CHECK(result2->at(0) == 0x00);
    CHECK(result2->at(1) == 0x01);
    CHECK(result2->at(2) == 0x02);
    CHECK(result2->at(3) == 0x03);
  }

  SUBCASE("uppercase hex")
  {
    auto result = util::parse_base16("DEADBEEF");
    REQUIRE(result);
    CHECK(result->size() == 4);
    CHECK(result->at(0) == 0xde);
    CHECK(result->at(1) == 0xad);
    CHECK(result->at(2) == 0xbe);
    CHECK(result->at(3) == 0xef);
  }

  SUBCASE("mixed case hex")
  {
    auto result = util::parse_base16("DeAdBeEf");
    REQUIRE(result);
    CHECK(result->size() == 4);
    CHECK(result->at(0) == 0xde);
    CHECK(result->at(1) == 0xad);
    CHECK(result->at(2) == 0xbe);
    CHECK(result->at(3) == 0xef);
  }

  SUBCASE("odd length string")
  {
    auto result = util::parse_base16("abc");
    REQUIRE(!result);
    CHECK(result.error().find("odd length") != std::string::npos);
  }

  SUBCASE("invalid characters")
  {
    auto result1 = util::parse_base16("xyz!");
    REQUIRE(!result1);
    CHECK(result1.error() == "invalid hex character at position 0: \"xyz!\"");

    auto result2 = util::parse_base16("12!4");
    REQUIRE(!result2);
    CHECK(result2.error() == "invalid hex character at position 2: \"12!4\"");

    auto result3 = util::parse_base16("abcg");
    REQUIRE(!result3);
    CHECK(result3.error() == "invalid hex character at position 3: \"abcg\"");
  }

  SUBCASE("round trip")
  {
    util::Bytes original = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    auto hex = util::format_base16(original);
    auto result = util::parse_base16(hex);
    REQUIRE(result);
    CHECK(*result == original);
  }
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
    CHECK(util::format_human_readable_diff(1'949, SUPT::binary) == "+1.9 KiB");
    CHECK(util::format_human_readable_diff(1'951, SUPT::binary) == "+1.9 KiB");
    CHECK(util::format_human_readable_diff(499'700, SUPT::binary)
          == "+488.0 KiB");
    CHECK(util::format_human_readable_diff(1'000'000, SUPT::binary)
          == "+976.6 KiB");
    CHECK(util::format_human_readable_diff(1'234'000, SUPT::binary)
          == "+1.2 MiB");
    CHECK(util::format_human_readable_diff(438'500'000, SUPT::binary)
          == "+418.2 MiB");
    CHECK(util::format_human_readable_diff(1'000'000'000, SUPT::binary)
          == "+953.7 MiB");
    CHECK(util::format_human_readable_diff(17'110'000'000, SUPT::binary)
          == "+15.9 GiB");

    CHECK(util::format_human_readable_diff(-1, SUPT::binary) == "-1 byte");
    CHECK(util::format_human_readable_diff(-42, SUPT::binary) == "-42 bytes");
    CHECK(util::format_human_readable_diff(-1'949, SUPT::binary) == "-1.9 KiB");
    CHECK(util::format_human_readable_diff(-1'951, SUPT::binary) == "-1.9 KiB");
    CHECK(util::format_human_readable_diff(-499'700, SUPT::binary)
          == "-488.0 KiB");
    CHECK(util::format_human_readable_diff(-1'000'000, SUPT::binary)
          == "-976.6 KiB");
    CHECK(util::format_human_readable_diff(-1'234'000, SUPT::binary)
          == "-1.2 MiB");
    CHECK(util::format_human_readable_diff(-438'500'000, SUPT::binary)
          == "-418.2 MiB");
    CHECK(util::format_human_readable_diff(-1'000'000'000, SUPT::binary)
          == "-953.7 MiB");
    CHECK(util::format_human_readable_diff(-17'110'000'000, SUPT::binary)
          == "-15.9 GiB");
  }

  SUBCASE("decimal")
  {
    CHECK(util::format_human_readable_diff(0, SUPT::decimal) == "0 bytes");
    CHECK(util::format_human_readable_diff(1, SUPT::decimal) == "+1 byte");
    CHECK(util::format_human_readable_diff(42, SUPT::decimal) == "+42 bytes");
    CHECK(util::format_human_readable_diff(1'949, SUPT::decimal) == "+1.9 kB");
    CHECK(util::format_human_readable_diff(1'951, SUPT::decimal) == "+2.0 kB");
    CHECK(util::format_human_readable_diff(499'700, SUPT::decimal)
          == "+499.7 kB");
    CHECK(util::format_human_readable_diff(1'000'000, SUPT::decimal)
          == "+1.0 MB");
    CHECK(util::format_human_readable_diff(1'234'000, SUPT::decimal)
          == "+1.2 MB");
    CHECK(util::format_human_readable_diff(438'500'000, SUPT::decimal)
          == "+438.5 MB");
    CHECK(util::format_human_readable_diff(1'000'000'000, SUPT::decimal)
          == "+1.0 GB");
    CHECK(util::format_human_readable_diff(17'110'000'000, SUPT::decimal)
          == "+17.1 GB");

    CHECK(util::format_human_readable_diff(-1, SUPT::decimal) == "-1 byte");
    CHECK(util::format_human_readable_diff(-42, SUPT::decimal) == "-42 bytes");
    CHECK(util::format_human_readable_diff(-1'949, SUPT::decimal) == "-1.9 kB");
    CHECK(util::format_human_readable_diff(-1'951, SUPT::decimal) == "-2.0 kB");
    CHECK(util::format_human_readable_diff(-499'700, SUPT::decimal)
          == "-499.7 kB");
    CHECK(util::format_human_readable_diff(-1'000'000, SUPT::decimal)
          == "-1.0 MB");
    CHECK(util::format_human_readable_diff(-1'234'000, SUPT::decimal)
          == "-1.2 MB");
    CHECK(util::format_human_readable_diff(-438'500'000, SUPT::decimal)
          == "-438.5 MB");
    CHECK(util::format_human_readable_diff(-1'000'000'000, SUPT::decimal)
          == "-1.0 GB");
    CHECK(util::format_human_readable_diff(-17'110'000'000, SUPT::decimal)
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
    CHECK(util::format_human_readable_size(1'949, SUPT::binary) == "1.9 KiB");
    CHECK(util::format_human_readable_size(1'951, SUPT::binary) == "1.9 KiB");
    CHECK(util::format_human_readable_size(499'700, SUPT::binary)
          == "488.0 KiB");
    CHECK(util::format_human_readable_size(1'000'000, SUPT::binary)
          == "976.6 KiB");
    CHECK(util::format_human_readable_size(1'234'000, SUPT::binary)
          == "1.2 MiB");
    CHECK(util::format_human_readable_size(438'500'000, SUPT::binary)
          == "418.2 MiB");
    CHECK(util::format_human_readable_size(1'000'000'000, SUPT::binary)
          == "953.7 MiB");
    CHECK(util::format_human_readable_size(17'110'000'000, SUPT::binary)
          == "15.9 GiB");
  }

  SUBCASE("decimal")
  {
    CHECK(util::format_human_readable_size(0, SUPT::decimal) == "0 bytes");
    CHECK(util::format_human_readable_size(1, SUPT::decimal) == "1 byte");
    CHECK(util::format_human_readable_size(42, SUPT::decimal) == "42 bytes");
    CHECK(util::format_human_readable_size(1'949, SUPT::decimal) == "1.9 kB");
    CHECK(util::format_human_readable_size(1'951, SUPT::decimal) == "2.0 kB");
    CHECK(util::format_human_readable_size(499'700, SUPT::decimal)
          == "499.7 kB");
    CHECK(util::format_human_readable_size(1'000'000, SUPT::decimal)
          == "1.0 MB");
    CHECK(util::format_human_readable_size(1'234'000, SUPT::decimal)
          == "1.2 MB");
    CHECK(util::format_human_readable_size(438'500'000, SUPT::decimal)
          == "438.5 MB");
    CHECK(util::format_human_readable_size(1'000'000'000, SUPT::decimal)
          == "1.0 GB");
    CHECK(util::format_human_readable_size(17'110'000'000, SUPT::decimal)
          == "17.1 GB");
  }
}

TEST_CASE("util::format_iso8601_timestamp")
{
  using util::TimePoint;
  using util::TimeZone;

  CHECK(util::format_iso8601_timestamp(TimePoint(0s), TimeZone::utc)
        == "1970-01-01T00:00:00");
  CHECK(util::format_iso8601_timestamp(TimePoint(1234567890s), TimeZone::utc)
        == "2009-02-13T23:31:30");
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

TEST_CASE("util::join_path_list")
{
  CHECK(util::join_path_list({}).empty());
#ifdef _WIN32
  CHECK(util::join_path_list({"a", "b"}) == "a;b");
#else
  CHECK(util::join_path_list({"a", "b"}) == "a:b");
#endif
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
  CHECK(*util::parse_duration("0s") == 0ms);
  CHECK(*util::parse_duration("2s") == 2000ms);
  CHECK(*util::parse_duration("1ms") == 1ms);
  CHECK(*util::parse_duration("500ms") == 500ms);
  CHECK(*util::parse_duration("1m") == 60000ms);
  CHECK(*util::parse_duration("2m") == 120000ms);
  CHECK(*util::parse_duration("1h") == 3600000ms);
  CHECK(*util::parse_duration("2h") == 7200000ms);
  CHECK(*util::parse_duration("1d") == 86400000ms);
  CHECK(*util::parse_duration("2d") == 172800000ms);

  CHECK(util::parse_duration("d").error() == "invalid unsigned integer: \"\"");
  CHECK(util::parse_duration("xd").error()
        == "invalid unsigned integer: \"x\"");
  CHECK(util::parse_duration("-2d").error()
        == "invalid unsigned integer: \"-2\"");

  CHECK(util::parse_duration("-2").error()
        == "invalid suffix (supported: ms (millisecond), s (second), m "
           "(minute), h (hour), d (day)): \"-2\"");
  CHECK(util::parse_duration("2x").error()
        == "invalid suffix (supported: ms (millisecond), s (second), m "
           "(minute), h (hour), d (day)): \"2x\"");
  CHECK(util::parse_duration("2").error()
        == "invalid suffix (supported: ms (millisecond), s (second), m "
           "(minute), h (hour), d (day)): \"2\"");
  CHECK(util::parse_duration("").error() == "invalid empty duration: \"\"");
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
  CHECK(*util::parse_size("78k") == h(78'000, SUPT::decimal));
  CHECK(*util::parse_size("78K") == h(78'000, SUPT::decimal));
  CHECK(*util::parse_size("1.1 M") == h(u64(1.1 * 1'000'000), SUPT::decimal));
  CHECK(*util::parse_size("438.55M")
        == h(u64(438.55 * 1'000'000), SUPT::decimal));
  CHECK(*util::parse_size("1 G") == h(1 * 1'000'000'000, SUPT::decimal));
  CHECK(*util::parse_size("2T")
        == h(u64(2) * 1'000'000 * 1'000'000, SUPT::decimal));

  // Binary suffixes
  CHECK(*util::parse_size("78 Ki") == h(78 * 1024, SUPT::binary));
  CHECK(*util::parse_size("1.1Mi") == h(u64(1.1 * 1024 * 1024), SUPT::binary));
  CHECK(*util::parse_size("438.55 Mi")
        == h(u64(438.55 * 1024 * 1024), SUPT::binary));
  CHECK(*util::parse_size("1Gi") == h(1 * 1024 * 1024 * 1024, SUPT::binary));
  CHECK(*util::parse_size("2 Ti")
        == h(u64(2) * 1024 * 1024 * 1024 * 1024, SUPT::binary));

  // With B suffix
  CHECK(*util::parse_size("9MB") == h(9 * 1'000'000, SUPT::decimal));
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
  CHECK(util::replace_all(std::string_view("xaxbc", 4), "x", "y") == "yayb");
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
  using Result = std::pair<std::string, std::optional<std::string>>;
  using std::nullopt;
  using util::split_once;

  SUBCASE("const char*")
  {
    CHECK(split_once("", '=') == Result("", nullopt));
    CHECK(split_once("a", '=') == Result("a", nullopt));
    CHECK(split_once("=a", '=') == Result("", "a"));
    CHECK(split_once("a=", '=') == Result("a", ""));
    CHECK(split_once("a==", '=') == Result("a", "="));
    CHECK(split_once("a=b", '=') == Result("a", "b"));
    CHECK(split_once("a=b=", '=') == Result("a", "b="));
    CHECK(split_once("a=b=c", '=') == Result("a", "b=c"));
    CHECK(split_once("x y", ' ') == Result("x", "y"));
  }

  SUBCASE("std::string&&")
  {
    CHECK(split_once(std::string(""), '=') == Result("", nullopt));
    CHECK(split_once(std::string("a"), '=') == Result("a", nullopt));
    CHECK(split_once(std::string("=a"), '=') == Result("", "a"));
    CHECK(split_once(std::string("a="), '=') == Result("a", ""));
    CHECK(split_once(std::string("a=="), '=') == Result("a", "="));
    CHECK(split_once(std::string("a=b"), '=') == Result("a", "b"));
    CHECK(split_once(std::string("a=b="), '=') == Result("a", "b="));
    CHECK(split_once(std::string("a=b=c"), '=') == Result("a", "b=c"));
    CHECK(split_once(std::string("x y"), ' ') == Result("x", "y"));
  }

  SUBCASE("std::string_view")
  {
    CHECK(split_once(std::string_view(""), '=') == Result("", nullopt));
    CHECK(split_once(std::string_view("a"), '=') == Result("a", nullopt));
    CHECK(split_once(std::string_view("=a"), '=') == Result("", "a"));
    CHECK(split_once(std::string_view("a="), '=') == Result("a", ""));
    CHECK(split_once(std::string_view("a=="), '=') == Result("a", "="));
    CHECK(split_once(std::string_view("a=b"), '=') == Result("a", "b"));
    CHECK(split_once(std::string_view("a=b="), '=') == Result("a", "b="));
    CHECK(split_once(std::string_view("a=b=c"), '=') == Result("a", "b=c"));
    CHECK(split_once(std::string_view("x y"), ' ') == Result("x", "y"));
  }
}

TEST_CASE("util::split_option_with_concat_path")
{
  using Result = std::pair<std::string_view, std::optional<std::string_view>>;
  using std::nullopt;
  const auto split = util::split_option_with_concat_path;

  CHECK(split("-I/c/foo") == Result("-I", "/c/foo"));
  CHECK(split("-W,path/c/foo") == Result("-W,path", "/c/foo"));
  CHECK(split("-DMACRO") == Result("-DMACRO", nullopt));
#ifdef _WIN32
  CHECK(split("-I/C:/foo") == Result("-I", "/C:/foo"));
  CHECK(split("-IC:/foo") == Result("-I", "C:/foo"));
  CHECK(split("-W,path/c:/foo") == Result("-W,path", "/c:/foo"));
  CHECK(split("-W,pathC:/foo") == Result("-W,path", "C:/foo"));
  CHECK(split("-opt:value") == Result("-opt:value", nullopt));
#endif
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
