// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include <ccache/compiler/msvc.hpp>
#include <ccache/context.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

const std::string_view k_default_prefix = "Note: including file:";

TEST_SUITE_BEGIN("msvc");

TEST_CASE("get_includes_from_msvc_show_includes")
{
  SUBCASE("Parse empty output")
  {
    std::string contents;
    const auto result = compiler::get_includes_from_msvc_show_includes(
      contents, k_default_prefix);
    CHECK(result.size() == 0);
  }

  SUBCASE("Parse real output")
  {
    std::string contents = R"(Just a line
Note: including file: F:/Projects/ccache/build-msvc/config.h
Note: including file: F:\Projects\ccache\unittest\../src/Context.hpp
Note: including file:  F:\Projects\ccache\src\Args.hpp
Note: including file:   F:\Projects\ccache\src\NonCopyable.hpp
Note: including file:   C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.33.31629\include\deque
)";
    const auto result = compiler::get_includes_from_msvc_show_includes(
      contents, k_default_prefix);
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "F:/Projects/ccache/build-msvc/config.h");
    CHECK(result[1] == R"(F:\Projects\ccache\unittest\../src/Context.hpp)");
    CHECK(result[2] == R"(F:\Projects\ccache\src\Args.hpp)");
    CHECK(result[3] == R"(F:\Projects\ccache\src\NonCopyable.hpp)");
    CHECK(
      result[4]
      == R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.33.31629\include\deque)");
  }

  SUBCASE("Parse output with CRLF")
  {
    std::string contents =
      "Note: including file: foo\r\n"
      "Note: including file: bar\r\n";
    const auto result = compiler::get_includes_from_msvc_show_includes(
      contents, k_default_prefix);
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "foo");
    CHECK(result[1] == "bar");
  }

  SUBCASE("Parse output with an empty entry")
  {
    std::string contents =
      "Note: including file: foo\n"
      "Note: including file: \n"
      "Note: including file:  bar\n";
    const auto result = compiler::get_includes_from_msvc_show_includes(
      contents, k_default_prefix);
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "foo");
    CHECK(result[1] == "bar");
  }

  SUBCASE("Parse output with a custom prefix")
  {
    std::string contents = R"(custom foo
custom   bar
Just a line with custom in the middle)";
    const auto result =
      compiler::get_includes_from_msvc_show_includes(contents, "custom");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "foo");
    CHECK(result[1] == "bar");
  }
}

TEST_CASE("get_includes_from_msvc_source_deps")
{
  SUBCASE("Simple case")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Source": "C:\\path\\to\\source.cpp",
    "Includes": [
      "C:\\path\\to\\header1.h",
      "C:\\path\\to\\header2.h"
    ]
  }
})";

    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    REQUIRE(includes_res);
    const auto& includes = *includes_res;
    REQUIRE(includes.size() == 2);
    CHECK(includes[0] == "C:\\path\\to\\header1.h");
    CHECK(includes[1] == "C:\\path\\to\\header2.h");
  }

  SUBCASE("Empty includes array")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Source": "C:\\path\\to\\source.cpp",
    "Includes": []
  }
})";

    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    REQUIRE(includes_res);
    CHECK(includes_res->empty());
  }

  SUBCASE("Escaped paths")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Source": "C:\\path\\to\\source.cpp",
    "Includes": [
      "C:\\path\\to\\header\"with\"quotes.h",
      "C:\\path\\to\\header\\with\\backslashes.h"
    ]
  }
})";

    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    REQUIRE(includes_res);
    const auto& includes = *includes_res;
    REQUIRE(includes.size() == 2);
    CHECK(includes[0] == "C:\\path\\to\\header\"with\"quotes.h");
    CHECK(includes[1] == "C:\\path\\to\\header\\with\\backslashes.h");
  }

  SUBCASE("Minified JSON")
  {
    std::string json =
      R"({"Version":"1.1","Data":{"Source":"C:\\source.cpp","Includes":["C:\\header1.h","C:\\header2.h"]}})";

    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    REQUIRE(includes_res);
    const auto& includes = *includes_res;
    REQUIRE(includes.size() == 2);
    CHECK(includes[0] == "C:\\header1.h");
    CHECK(includes[1] == "C:\\header2.h");
  }

  SUBCASE("UTF-8 paths")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Source": "C:\\日本語\\source.cpp",
    "Includes": [
      "C:\\日本語\\header1.h",
      "C:\\Ελληνικά\\header2.h"
    ]
  }
})";

    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    REQUIRE(includes_res);
    const auto& includes = *includes_res;
    REQUIRE(includes.size() == 2);
    CHECK(includes[0] == "C:\\日本語\\header1.h");
    CHECK(includes[1] == "C:\\Ελληνικά\\header2.h");
  }

  SUBCASE("Invalid JSON")
  {
    auto includes_res =
      compiler::get_includes_from_msvc_source_deps("not json");
    REQUIRE(!includes_res);
    CHECK(includes_res.error().find("Expected object") != std::string::npos);
  }

  SUBCASE("Unicode escape sequences are rejected")
  {
    std::string json = R"({
  "Version": "1.1",
  "Data": {
    "Includes": ["C:\\path\\to\\\u65E5\u672C\u8A9E.h"]
  }
})";
    auto includes_res = compiler::get_includes_from_msvc_source_deps(json);
    CHECK_FALSE(includes_res);
  }
}

TEST_SUITE_END();
