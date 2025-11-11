// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/context.hpp>
#include <ccache/util/json.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("json");

TEST_CASE("SimpleJsonParser")
{
  SUBCASE("Parse MSVC /sourceDependencies file")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Source": "C:\\path\\to\\source.cpp",
    "ProvidedModule": "",
    "Includes": [
      "C:\\path\\to\\header\"with\"quotes.h",
      "C:\\path\\to\\header\\with\\backslashes.h",
      "C:\\日本語\\header1.h"
    ]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 3);
    CHECK((*includes)[0] == "C:\\path\\to\\header\"with\"quotes.h");
    CHECK((*includes)[1] == "C:\\path\\to\\header\\with\\backslashes.h");
    CHECK((*includes)[2] == "C:\\日本語\\header1.h");
  }

  SUBCASE("Empty array")
  {
    std::string json = R"({
  "Data": {
    "Includes": []
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    CHECK(includes->empty());
  }

  SUBCASE("Single element array")
  {
    std::string json = R"({
  "Data": {
    "Includes": ["single.h"]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 1);
    CHECK((*includes)[0] == "single.h");
  }

  SUBCASE("Array with whitespace variations")
  {
    std::string json = R"({
"Data":{"Includes":["a.h"  ,  "b.h","c.h"]}
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 3);
    CHECK((*includes)[0] == "a.h");
    CHECK((*includes)[1] == "b.h");
    CHECK((*includes)[2] == "c.h");
  }

  SUBCASE("Escape sequences")
  {
    std::string json = R"({
  "Data": {
    "Includes": [
      "path\\with\\backslashes",
      "string\"with\"quotes",
      "line1\nline2",
      "tab\tseparated",
      "carriage\rreturn",
      "form\ffeed",
      "back\bspace",
      "forward/slash"
    ]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 8);
    CHECK((*includes)[0] == "path\\with\\backslashes");
    CHECK((*includes)[1] == "string\"with\"quotes");
    CHECK((*includes)[2] == "line1\nline2");
    CHECK((*includes)[3] == "tab\tseparated");
    CHECK((*includes)[4] == "carriage\rreturn");
    CHECK((*includes)[5] == "form\ffeed");
    CHECK((*includes)[6] == "back\bspace");
    CHECK((*includes)[7] == "forward/slash");
  }

  SUBCASE("UTF-8 characters")
  {
    std::string json = R"({
  "Data": {
    "Includes": [
      "日本語.h",
      "中文.cpp",
      "한글.hpp",
      "emoji😀.c",
      "Ελληνικά.h"
    ]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 5);
    CHECK((*includes)[0] == "日本語.h");
    CHECK((*includes)[1] == "中文.cpp");
    CHECK((*includes)[2] == "한글.hpp");
    CHECK((*includes)[3] == "emoji😀.c");
    CHECK((*includes)[4] == "Ελληνικά.h");
  }

  SUBCASE("Nested objects")
  {
    std::string json = R"({
  "Level1": {
    "Level2": {
      "Level3": {
        "Files": ["deep.h"]
      }
    }
  }
})";

    util::SimpleJsonParser parser(json);
    auto files = parser.get_string_array(".Level1.Level2.Level3.Files");
    REQUIRE(files);
    REQUIRE(files->size() == 1);
    CHECK((*files)[0] == "deep.h");
  }

  SUBCASE("Object with multiple keys")
  {
    std::string json = R"({
  "Version": "1.0",
  "Data": {
    "Source": "main.cpp",
    "Includes": ["header.h"],
    "Flags": ["-O2", "-Wall"]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 1);
    CHECK((*includes)[0] == "header.h");

    util::SimpleJsonParser parser2(json);
    auto flags = parser2.get_string_array(".Data.Flags");
    REQUIRE(flags);
    REQUIRE(flags->size() == 2);
    CHECK((*flags)[0] == "-O2");
    CHECK((*flags)[1] == "-Wall");
  }

  SUBCASE("Skip non-target values")
  {
    std::string json = R"({
  "Other": {
    "NestedArray": [1, 2, 3],
    "NestedObject": {"key": "value"}
  },
  "Data": {
    "Includes": ["target.h"]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 1);
    CHECK((*includes)[0] == "target.h");
  }

  SUBCASE("Empty strings in array")
  {
    std::string json = R"({
  "Data": {
    "Includes": ["", "file.h", ""]
  }
})";

    util::SimpleJsonParser parser(json);
    auto includes = parser.get_string_array(".Data.Includes");
    REQUIRE(includes);
    REQUIRE(includes->size() == 3);
    CHECK((*includes)[0] == "");
    CHECK((*includes)[1] == "file.h");
    CHECK((*includes)[2] == "");
  }

  SUBCASE("Error: Invalid filter (no leading dot)")
  {
    std::string json = R"({"Data": {"Includes": []}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array("Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "Invalid filter: must start with '.'");
  }

  SUBCASE("Error: Invalid filter (empty)")
  {
    std::string json = R"({"Data": {"Includes": []}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array("");
    CHECK(!result);
    CHECK(result.error() == "Invalid filter: must start with '.'");
  }

  SUBCASE("Error: Key not found")
  {
    std::string json = R"({"Data": {"Other": []}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error().find("not found") != std::string::npos);
  }

  SUBCASE("Error: Not an array")
  {
    std::string json = R"({"Data": {"Includes": "not-an-array"}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error().find("Expected array") != std::string::npos);
  }

  SUBCASE("Error: Not an object")
  {
    std::string json = R"({"Data": "not-an-object"})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error().find("Expected object") != std::string::npos);
  }

  SUBCASE("Error: Unterminated string")
  {
    std::string json = R"({"Data": {"Includes": ["unterminated]}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "Unterminated string");
  }

  SUBCASE("Error: Unterminated array")
  {
    std::string json = R"({"Data": {"Includes": ["file.h")";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "Unterminated array");
  }

  SUBCASE("Error: Invalid escape sequence")
  {
    std::string json = R"({"Data": {"Includes": ["invalid\xescape"]}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error().find("Unknown escape sequence") != std::string::npos);
  }

  SUBCASE("Error: \\uXXXX escape sequence not supported")
  {
    std::string json = R"({"Data": {"Includes": ["unicode\u0041char"]}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "\\uXXXX escape sequences are not supported");
  }

  SUBCASE("Error: \\uXXXX in nested object")
  {
    std::string json =
      R"({"Data": {"Key": "value\u1234", "Includes": ["file.h"]}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "\\uXXXX escape sequences are not supported");
  }

  SUBCASE("Error: Root is not an object")
  {
    std::string json = R"(["array", "at", "root"])";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "Expected object at root");
  }

  SUBCASE("Error: Non-string element in array")
  {
    std::string json = R"({"Data": {"Includes": ["file.h", 123]}})";
    util::SimpleJsonParser parser(json);
    auto result = parser.get_string_array(".Data.Includes");
    CHECK(!result);
    CHECK(result.error() == "Expected string in array");
  }
}

TEST_SUITE_END();
