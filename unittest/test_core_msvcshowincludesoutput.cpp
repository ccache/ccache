// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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
#include <ccache/core/msvcshowincludesoutput.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

static const std::string defaultPrefix = "Note: including file:";

TEST_SUITE_BEGIN("MsvcShowIncludesOutput");

TEST_CASE("MsvcShowIncludesOutput::get_includes")
{
  SUBCASE("Parse empty output")
  {
    std::string contents;
    const auto result =
      core::MsvcShowIncludesOutput::get_includes(contents, defaultPrefix);
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
    const auto result =
      core::MsvcShowIncludesOutput::get_includes(contents, defaultPrefix);
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
    const auto result =
      core::MsvcShowIncludesOutput::get_includes(contents, defaultPrefix);
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
    const auto result =
      core::MsvcShowIncludesOutput::get_includes(contents, defaultPrefix);
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
      core::MsvcShowIncludesOutput::get_includes(contents, "custom");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "foo");
    CHECK(result[1] == "bar");
  }
}

TEST_CASE("MsvcShowIncludesOutput::strip_includes")
{
  Context ctx;
  const util::Bytes input = util::to_span(
    "First\n"
    "Note: including file: foo\n"
    "Second\n");

  SUBCASE("Empty output")
  {
    const util::Bytes result =
      core::MsvcShowIncludesOutput::strip_includes(ctx, {});
    CHECK(result.size() == 0);
  }

  SUBCASE("Feature disabled")
  {
    const util::Bytes result =
      core::MsvcShowIncludesOutput::strip_includes(ctx, util::Bytes(input));
    CHECK(result == input);
  }

  ctx.auto_depend_mode = true;

  SUBCASE("Wrong compiler")
  {
    const util::Bytes result =
      core::MsvcShowIncludesOutput::strip_includes(ctx, util::Bytes(input));
    CHECK(result == input);
  }

  ctx.config.set_compiler_type(CompilerType::msvc);

  SUBCASE("Simple output")
  {
    const util::Bytes result =
      core::MsvcShowIncludesOutput::strip_includes(ctx, util::Bytes(input));
    CHECK(result == util::to_span("First\nSecond\n"));
  }

  SUBCASE("Empty lines")
  {
    const util::Bytes result = core::MsvcShowIncludesOutput::strip_includes(
      ctx,
      util::to_span("First\n"
                    "\n"
                    "Note: including file: foo\n"
                    "\n"
                    "Second\n"
                    "\n"));
    CHECK(result == util::to_span("First\n\n\nSecond\n\n"));
  }

  SUBCASE("CRLF")
  {
    const util::Bytes result = core::MsvcShowIncludesOutput::strip_includes(
      ctx,
      util::to_span("First\r\n"
                    "Note: including file: foo\r\n"
                    "Second\r\n"));
    CHECK(result == util::to_span("First\r\nSecond\r\n"));
  }

  SUBCASE("Custom prefix")
  {
    ctx.config.set_msvc_dep_prefix("custom");
    const util::Bytes result = core::MsvcShowIncludesOutput::strip_includes(
      ctx,
      util::to_span("First\n"
                    "custom: including file: foo\n"
                    "Second\n"
                    "Third custom line\n"));
    CHECK(result == util::to_span("First\nSecond\nThird custom line\n"));
  }
}

TEST_SUITE_END();
