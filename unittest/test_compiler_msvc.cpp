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
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

#include <string>

namespace fs = util::filesystem;

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

namespace {

// A path goes into the JSON with every backslash doubled, which is how MSVC
// writes a Windows path.
std::string
json_path(const fs::path& path)
{
  return util::replace_all(util::pstr(path).str(), "\\", "\\\\");
}

std::string
source_dependencies(const std::string& source,
                    const std::string& include,
                    const std::string& other_include,
                    const std::string& header_unit,
                    const std::string& bmi)
{
  return FMT(R"({{
    "Version": "1.2",
    "Data": {{
        "Source": "{}",
        "ProvidedModule": "",
        "Includes": [
            "{}",
            "{}"
        ],
        "ImportedHeaderUnits": [
            {{
                "Header": "{}",
                "BMI": "{}"
            }}
        ]
    }}
}})",
             source,
             include,
             other_include,
             header_unit,
             bmi);
}

} // namespace

TEST_CASE("rewrite_paths_in_source_dependencies")
{
  Context ctx;

  const fs::path cwd = ctx.actual_cwd;
  const fs::path elsewhere = cwd.parent_path();

  const auto content = source_dependencies(json_path(cwd / "test.cpp"),
                                           json_path(cwd / "sub" / "dep.h"),
                                           json_path(elsewhere / "other.h"),
                                           json_path(cwd / "unit.h"),
                                           json_path(cwd / "unit.h.ifc"));

  SUBCASE("Base directory not in file content")
  {
#ifdef _WIN32
    ctx.config.set_base_dirs({"C:/foo/bar"});
#else
    ctx.config.set_base_dirs({"/foo/bar"});
#endif
    CHECK(!compiler::rewrite_paths_in_source_dependencies(ctx, ""));
    CHECK(!compiler::rewrite_paths_in_source_dependencies(ctx, content));
  }

  SUBCASE("Base directory in file content but not matching")
  {
    ctx.config.set_base_dirs({(elsewhere / "other").string()});
    CHECK(!compiler::rewrite_paths_in_source_dependencies(ctx, content));
  }

  SUBCASE("Paths under the base directory become relative")
  {
    ctx.config.set_base_dirs({cwd.string()});
    const auto actual =
      compiler::rewrite_paths_in_source_dependencies(ctx, content);
    const auto expected =
      source_dependencies("test.cpp",
                          json_path(fs::path("sub") / "dep.h"),
                          json_path(elsewhere / "other.h"),
                          "unit.h",
                          "unit.h.ifc");
    REQUIRE(actual);
    CHECK(*actual == expected);
  }
}

TEST_SUITE_END();
