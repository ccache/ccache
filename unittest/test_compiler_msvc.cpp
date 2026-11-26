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
#include <ccache/util/filesystem.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

namespace fs = util::filesystem;

using namespace std::string_view_literals;

static const std::string defaultPrefix = "Note: including file:";

TEST_SUITE_BEGIN("msvc");

TEST_CASE("get_includes_from_msvc_show_includes")
{
  SUBCASE("Parse empty output")
  {
    std::string contents;
    const auto result =
      compiler::get_includes_from_msvc_show_includes(contents, defaultPrefix);
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
      compiler::get_includes_from_msvc_show_includes(contents, defaultPrefix);
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
      compiler::get_includes_from_msvc_show_includes(contents, defaultPrefix);
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
      compiler::get_includes_from_msvc_show_includes(contents, defaultPrefix);
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

TEST_CASE("strip_includes_from_msvc_show_includes")
{
  Context ctx;
  const util::Bytes input = util::to_span(
    "First\n"
    "Note: including file: foo\n"
    "Second\n");

  SUBCASE("Empty output")
  {
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, {});
    CHECK(result.size() == 0);
  }

  SUBCASE("Feature disabled")
  {
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, util::Bytes(input));
    CHECK(result == input);
  }

  ctx.auto_depend_mode = true;

  SUBCASE("Wrong compiler")
  {
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, util::Bytes(input));
    CHECK(result == input);
  }

  ctx.config.set_compiler_type(CompilerType::msvc);

  SUBCASE("Simple output")
  {
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, util::Bytes(input));
    CHECK(result == util::to_span("First\nSecond\n"));
  }

  SUBCASE("Empty lines")
  {
    const util::Bytes result = compiler::strip_includes_from_msvc_show_includes(
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
    const util::Bytes result = compiler::strip_includes_from_msvc_show_includes(
      ctx,
      util::to_span("First\r\n"
                    "Note: including file: foo\r\n"
                    "Second\r\n"));
    CHECK(result == util::to_span("First\r\nSecond\r\n"));
  }

  SUBCASE("Custom prefix")
  {
    ctx.config.set_msvc_dep_prefix("custom");
    const util::Bytes result = compiler::strip_includes_from_msvc_show_includes(
      ctx,
      util::to_span("First\n"
                    "custom: including file: foo\n"
                    "Second\n"
                    "Third custom line\n"));
    CHECK(result == util::to_span("First\nSecond\nThird custom line\n"));
  }

  SUBCASE("NVCC auto depend mode")
  {
    ctx.config.set_compiler_type(CompilerType::nvcc);
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, util::Bytes(input));
    CHECK(result == util::to_span("First\nSecond\n"));
  }

  SUBCASE("NVCC explicit showIncludes")
  {
    ctx.auto_depend_mode = false;
    ctx.config.set_compiler_type(CompilerType::nvcc);
    ctx.config.set_depend_mode(true);
    ctx.args_info.generating_includes = true;
    const util::Bytes result =
      compiler::strip_includes_from_msvc_show_includes(ctx, util::Bytes(input));
    CHECK(result == input);
  }
}

TEST_CASE("parse_msvc_path_map")
{
  SUBCASE("Backslashes")
  {
    const auto map = compiler::parse_msvc_path_map(R"(C:\src\=X:\)");
    REQUIRE(map);
    CHECK(map->old_path == R"(C:\src\)");
    CHECK(map->new_path == R"(X:\)");
  }

  SUBCASE("Slashes, with the new path kept literally")
  {
    const auto map = compiler::parse_msvc_path_map("C:/src/=X:/");
    REQUIRE(map);
    CHECK(map->old_path == "C:/src/");
    CHECK(map->new_path == "X:/");
  }

  SUBCASE("Invalid")
  {
    for (const auto value : {R"(C:\src=)"sv, R"(C:\src\)"sv, ""sv}) {
      CAPTURE(value);
      CHECK(!compiler::parse_msvc_path_map(value));
    }
    CHECK(!compiler::parse_msvc_path_map("C:\\\xc3\xa4\\="sv));
  }
}

TEST_CASE("apply_msvc_path_map")
{
  const compiler::MsvcPathMap map{R"(C:\work\)", R"(W:\)"};
  CHECK(compiler::apply_msvc_path_map(map, R"(C:\work\a\build)")
        == R"(W:\a\build)");
  CHECK(compiler::apply_msvc_path_map(map, "c:/WORK/a") == R"(W:\a)");
  CHECK(compiler::apply_msvc_path_map(map, R"(C:\work)") == R"(W:\)");
  CHECK(!compiler::apply_msvc_path_map(map, R"(C:\work2\build)"));
  CHECK(!compiler::apply_msvc_path_map(map, R"(D:\work\build)"));
}

TEST_CASE("select_msvc_root_map")
{
  const std::vector<fs::path> base_dirs{R"(C:\work\a)"};
  const std::string back = R"(C:\work\a\=N)";
  const std::string forward = "C:/work/a/=N";

  SUBCASE("The pair in either order")
  {
    for (const auto& maps : {
           std::vector{back,    forward},
           std::vector{forward, back   }
    }) {
      const auto map = compiler::select_msvc_root_map(maps, base_dirs);
      REQUIRE(map);
      CHECK(map->old_path == R"(C:\work\a\)");
      CHECK(map->new_path == "N");
    }
  }

  SUBCASE("Not without exactly the pair")
  {
    const std::vector<std::vector<std::string>> cases{
      {back},
      {forward},
      {back, forward, R"(D:\sdk\=S)"},
      {back, "C:/work/a/=M"},
      {back, back},
      {R"(C:\work/a\=N)", forward},
      {R"(C:\work\=N)", "C:/work/=N"},
      {R"(c:\work\a\=N)", "c:/work/a/=N"},
    };
    for (const auto& maps : cases) {
      CAPTURE(maps);
      CHECK(!compiler::select_msvc_root_map(maps, base_dirs));
    }
  }

  SUBCASE("Not without exactly one base directory")
  {
    CHECK(!compiler::select_msvc_root_map({back, forward}, {}));
    CHECK(!compiler::select_msvc_root_map(
      {back, forward}, {fs::path(R"(C:\work\a)"), fs::path(R"(D:\other)")}));
  }
}

#ifdef _WIN32
TEST_CASE("msvc_object_dir")
{
  const fs::path cwd = R"(C:\work\a\build)";
  CHECK(compiler::msvc_object_dir(cwd, R"(sub\foo.obj)")
        == fs::path(R"(C:\work\a\build\sub)"));
  CHECK(compiler::msvc_object_dir(cwd, R"(..\x\..\obj\foo.obj)")
        == fs::path(R"(C:\work\a\obj)"));
}
#endif

TEST_CASE("msvc_output_names_root")
{
  const auto root = R"(C:\work\a\)"sv;
  CHECK(compiler::msvc_output_names_root(root, R"(C:\work\a\x.h(1): warning)"));
  CHECK(compiler::msvc_output_names_root(root, "note: c:/WORK/a/x.h"));
  CHECK(compiler::msvc_output_names_root(root, R"(in C:\work\a)"));
  // A longer directory name starting with the root is accepted as a match.
  CHECK(compiler::msvc_output_names_root(root, R"(C:\work\ab\x.h)"));
  CHECK(!compiler::msvc_output_names_root(root, R"(C:\work\x.h)"));
  CHECK(!compiler::msvc_output_names_root(root, ""));
}

TEST_SUITE_END();
