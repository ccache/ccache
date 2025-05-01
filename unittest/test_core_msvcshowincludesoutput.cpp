// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/compiler.hpp>
#include <ccache/context.hpp>
#include <ccache/core/msvcshowincludesoutput.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

using namespace ccache;
using namespace std::string_view_literals;

TEST_SUITE_BEGIN("MsvcShowIncludesOutput");

TEST_CASE("MsvcShowIncludesOutput::get_includes")
{
  Context ctx;
  ctx.auto_depend_mode = true;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  const auto defaultPrefix = ctx.config.msvc_dep_prefix();

  SUBCASE("Parse empty output")
  {
    std::string_view text = {};
    const auto console = compiler::Console::process(ctx, text);
    CHECK(console.msvc().show_includes().size() == 0);
  }

  SUBCASE("Parse real output")
  {
    const auto text = R"(Just a line
Note: including file: F:/Projects/ccache/build-msvc/config.h
Note: including file: F:\Projects\ccache\unittest\../src/Context.hpp
Note: including file:  F:\Projects\ccache\src\Args.hpp
Note: including file:   F:\Projects\ccache\src\NonCopyable.hpp
Note: including file:   C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.33.31629\include\deque
)"sv;
    const auto console = compiler::Console::process(ctx, text);
    REQUIRE(console.msvc().show_includes().size() == 5);
    CHECK(console.msvc().show_includes()[0]
          == "F:/Projects/ccache/build-msvc/config.h");
    CHECK(console.msvc().show_includes()[1]
          == R"(F:\Projects\ccache\unittest\../src/Context.hpp)");
    CHECK(console.msvc().show_includes()[2]
          == R"(F:\Projects\ccache\src\Args.hpp)");
    CHECK(console.msvc().show_includes()[3]
          == R"(F:\Projects\ccache\src\NonCopyable.hpp)");
    CHECK(
      console.msvc().show_includes()[4]
      == R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.33.31629\include\deque)");
  }

  SUBCASE("Parse output with CRLF")
  {
    const auto text =
      "Note: including file: foo\r\n"sv
      "Note: including file: bar\r\n"sv;
    const auto console = compiler::Console::process(ctx, text);
    REQUIRE(console.msvc().show_includes().size() == 2);
    CHECK(console.msvc().show_includes()[0] == "foo");
    CHECK(console.msvc().show_includes()[1] == "bar");
  }

  SUBCASE("Parse output with an empty entry")
  {
    const auto text =
      "Note: including file: foo\n"sv
      "Note: including file: \n"sv
      "Note: including file:  bar\n"sv;
    const auto console = compiler::Console::process(ctx, text);
    REQUIRE(console.msvc().show_includes().size() == 2);
    CHECK(console.msvc().show_includes()[0] == "foo");
    CHECK(console.msvc().show_includes()[1] == "bar");
  }

  SUBCASE("Parse output with a custom prefix")
  {
    ctx.config.set_msvc_dep_prefix("custom");

    const auto text = R"(custom foo
custom   bar
Just a line with custom in the middle)"sv;
    const auto console = compiler::Console::process(ctx, text);
    REQUIRE(console.msvc().show_includes().size() == 2);
    CHECK(console.msvc().show_includes()[0] == "foo");
    CHECK(console.msvc().show_includes()[1] == "bar");

    ctx.config.set_msvc_dep_prefix(defaultPrefix);
  }
}

TEST_CASE("MsvcShowIncludesOutput::strip_includes")
{
  Context ctx;
  ctx.auto_depend_mode = true;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  const auto defaultPrefix = ctx.config.msvc_dep_prefix();

  const auto shared_text =
    "First\n"sv
    "Note: including file: foo\n"sv
    "Second\n"sv;

  SUBCASE("Empty output")
  {
    const auto console = compiler::Console::process(ctx, {});
    CHECK(console.stdout_text().filtered().size() == 0);
  }

  SUBCASE("Feature disabled")
  {
    ctx.auto_depend_mode = false;

    const util::Bytes data = util::to_span(shared_text);
    const auto console = compiler::Console::process(ctx, shared_text);
    CHECK(console.stdout_text().filtered() == shared_text);

    ctx.auto_depend_mode = true;
  }

  SUBCASE("Wrong compiler")
  {
    ctx.config.set_compiler_type(Compiler::type::auto_guess);

    const auto console = compiler::Console::process(ctx, shared_text);
    CHECK(console.stdout_text().filtered() == shared_text);

    ctx.config.set_compiler_type(Compiler::type::msvc);
  }

  SUBCASE("Simple output")
  {
    const auto console = compiler::Console::process(ctx, shared_text);
    CHECK(console.stdout_text().filtered() == "First\nSecond\n");
  }

  SUBCASE("Empty lines")
  {
    const auto custom_text =
      "First\n"sv
      "\n"sv
      "Note: including file: foo\n"sv
      "\n"sv
      "Second\n"sv
      "\n"sv;
    const auto parse = compiler::Console::process(ctx, custom_text);
    CHECK(parse.stdout_text().filtered() == "First\n\n\nSecond\n\n");
  }

  SUBCASE("CRLF")
  {
    const auto custom_text =
      "First\r\n"sv
      "Note: including file: foo\r\n"sv
      "Second\r\n"sv;
    const auto console = compiler::Console::process(ctx, custom_text);
    CHECK(console.stdout_text().filtered() == "First\r\nSecond\r\n");
  }

  SUBCASE("Custom prefix")
  {
    ctx.config.set_msvc_dep_prefix("custom");

    const auto custom_text =
      "First\n"sv
      "custom: including file: foo\n"sv
      "Second\n"sv
      "Third custom line\n"sv;
    const auto console = compiler::Console::process(ctx, custom_text);
    CHECK(console.stdout_text().filtered()
          == "First\nSecond\nThird custom line\n");

    ctx.config.set_msvc_dep_prefix(defaultPrefix);
  }
}

TEST_SUITE_END();
