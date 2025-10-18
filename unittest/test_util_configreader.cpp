// Copyright (C) 2025 Joel Rosdahl and other contributors
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
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <ccache/util/configreader.hpp>

#include <doctest/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618

using util::ConfigReader;

TEST_SUITE("util::ConfigReader")
{
  TEST_CASE("missing equal sign")
  {
    ConfigReader reader("key");
    auto result = reader.read_next_item();
    REQUIRE(!result);
    CHECK(result.error().line_number == 1);
    CHECK(result.error().message == "missing equal sign");
  }

  TEST_CASE("indented key")
  {
    ConfigReader reader(" key = value");
    auto result = reader.read_next_item();
    REQUIRE(!result);
    CHECK(result.error().line_number == 1);
    CHECK(result.error().message == "indented key");
  }

  TEST_CASE("read_next_item: simple key=value")
  {
    ConfigReader reader("key = value");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.line_number == 1);
    CHECK(item.key == "key");
    CHECK(item.value == "value");
    auto eof = reader.read_next_item();
    REQUIRE(eof);
    CHECK(!*eof);
  }

  TEST_CASE("read_next_item: multiple items")
  {
    ConfigReader reader("key1  =   value1 \nkey2=value2\n");
    auto result1 = reader.read_next_item();
    REQUIRE(result1);
    auto& item1 = **result1;
    CHECK(item1.key == "key1");
    CHECK(item1.value == "value1");

    auto result2 = reader.read_next_item();
    REQUIRE(result2);
    auto& item2 = **result2;
    CHECK(item2.key == "key2");
    CHECK(item2.value == "value2");

    auto eof = reader.read_next_item();
    REQUIRE(eof);
    CHECK(!*eof);
  }

  TEST_CASE("read_next_item: indented continuation")
  {
    ConfigReader reader("key = a\n  b\n  c");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.key == "key");
    CHECK(item.value == "a b c");
  }

  TEST_CASE("read_next_item: empty value on first line")
  {
    ConfigReader reader("key =\n  b\n  c");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.key == "key");
    CHECK(item.value == "b c");
  }

  TEST_CASE("read_next_item: comments are skipped")
  {
    ConfigReader reader("key = a\n  b\n# comment\n  c");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.value == "a b c");
  }

  TEST_CASE("read_next_item: blank lines are skipped")
  {
    ConfigReader reader("key = a\n  b\n\n  c");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.value == "a b c");
  }

  TEST_CASE("read_next_item: inline comments preserved")
  {
    ConfigReader reader("key = a # not a comment");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.value == "a # not a comment");
  }

  TEST_CASE("read_next_item: different comments")
  {
    ConfigReader reader(
      "world =\n"
      " a\n"
      " b # not a comment\n"
      "\n"
      "# nonindented comment\n"
      " # indented comment\n"
      " c = d");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.key == "world");
    CHECK(item.value == "a b # not a comment c = d");
  }

  TEST_CASE("read_next_item: leading comments are skipped")
  {
    ConfigReader reader("# comment\nkey = value");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.line_number == 2);
    CHECK(item.key == "key");
  }

  TEST_CASE("read_next_item: tab indentation")
  {
    ConfigReader reader("key = a\n\tb\n\tc");
    auto result = reader.read_next_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.value == "a b c");
  }

  TEST_CASE("read_next_item: empty config")
  {
    ConfigReader reader("");
    auto result = reader.read_next_item();
    REQUIRE(result);
    CHECK(!*result);
  }

  TEST_CASE("read_next_item: only comments")
  {
    ConfigReader reader("# comment1\n# comment2");
    auto result = reader.read_next_item();
    REQUIRE(result);
    CHECK(!*result);
  }

  TEST_CASE("read_next_item: CRLF line endings")
  {
    // Use CRLF line endings as might appear on Windows files
    ConfigReader reader("key = a\r\n  b\r\n# comment\r\n  c\r\nother = x\r\n");
    auto result1 = reader.read_next_item();
    REQUIRE(result1);
    auto& item1 = **result1;
    CHECK(item1.key == "key");
    CHECK(item1.value == "a b c");

    auto result2 = reader.read_next_item();
    REQUIRE(result2);
    auto& item2 = **result2;
    CHECK(item2.key == "other");
    CHECK(item2.value == "x");
  }

  TEST_CASE("read_next_raw_item: simple key=value")
  {
    ConfigReader reader("key = value");
    auto result = reader.read_next_raw_item();
    REQUIRE(result);
    auto& item = **result;
    CHECK(item.key == "key");
    CHECK(item.value_start_pos == 6); // After "key = "
    CHECK(item.value_length == 5);    // Length of "value"
  }

  TEST_CASE("read_next_raw_item: preserves embedded comments and blank lines")
  {
    std::string config = "key = a\n  b\n\n# comment\n \n  c\nother = x";
    ConfigReader reader(config);

    auto result1 = reader.read_next_raw_item();
    REQUIRE(result1);
    auto& item1 = **result1;
    CHECK(item1.key == "key");
    // Raw value should include "a\n  b\n\n# comment\n \n  c" (no trailing
    // newline)
    std::string raw_value =
      config.substr(item1.value_start_pos, item1.value_length);
    CHECK(raw_value == "a\n  b\n\n# comment\n \n  c");

    auto result2 = reader.read_next_raw_item();
    REQUIRE(result2);
    auto& item2 = **result2;
    CHECK(item2.key == "other");
  }

  TEST_CASE("read_next_raw_item: multiple items")
  {
    std::string config = "key1 = value1\nkey2 = value2";
    ConfigReader reader(config);

    auto result1 = reader.read_next_raw_item();
    REQUIRE(result1);
    auto& item1 = **result1;
    CHECK(item1.key == "key1");
    std::string value1 =
      config.substr(item1.value_start_pos, item1.value_length);
    CHECK(value1 == "value1");

    auto result2 = reader.read_next_raw_item();
    REQUIRE(result2);
    auto& item2 = **result2;
    CHECK(item2.key == "key2");
    std::string value2 =
      config.substr(item2.value_start_pos, item2.value_length);
    CHECK(value2 == "value2");
  }

  TEST_CASE("read_next_raw_item: EOF handling")
  {
    ConfigReader reader("key = value");
    auto result = reader.read_next_raw_item();
    REQUIRE(result);
    REQUIRE(*result);
    auto eof = reader.read_next_raw_item();
    REQUIRE(eof);
    CHECK(!*eof);
  }

  TEST_CASE("read_next_raw_item: CRLF preserves raw blocks")
  {
    std::string config =
      "key = a\r\n"
      "  b\r\n"
      "\r\n"
      "# comment\r\n"
      "  c\r\n"
      "other = x\r\n";

    ConfigReader reader(config);
    auto res = reader.read_next_raw_item();
    REQUIRE(res);
    auto& raw_item = **res;
    CHECK(raw_item.key == "key");

    // Extract raw substring and verify it preserves embedded blank/comment
    // lines (without the trailing newline)
    std::string raw_value =
      config.substr(raw_item.value_start_pos, raw_item.value_length);

    CHECK(raw_value == "a\r\n  b\r\n\r\n# comment\r\n  c");

    auto res2 = reader.read_next_raw_item();
    REQUIRE(res2);
    auto& raw_item2 = **res2;
    CHECK(raw_item2.key == "other");
  }
}
