// Copyright (C) 2010-2026 Joel Rosdahl and other contributors
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
#include <ccache/hash.hpp>
#include <ccache/hashutil.hpp>
#include <ccache/util/cpu.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/format.hpp>

#include <doctest/doctest.h>

#include <sys/stat.h>

using TestUtil::TestContext;

static bool
hco(Hash& hash, const std::string& command, const std::string& compiler)
{
#ifdef _WIN32
  REQUIRE(util::write_file("command.bat", FMT("@echo off\r\n{}\r\n", command)));
  return hash_command_output(hash, "command.bat", compiler);
#else
  REQUIRE(util::write_file("command.sh", FMT("#!/bin/sh\n{}\n", command)));
  chmod("command.sh", 0555);
  return hash_command_output(hash, "./command.sh", compiler);
#endif
}

using SourceCodePatternChecker = SourceCodeScanResult (*)(std::string_view);

static void
check_temporal_macros(SourceCodePatternChecker check)
{
  const std::string_view time_start =
    "__TIME__\n"
    "int a;\n";
  const std::string_view time_middle =
    "#define a __TIME__\n"
    "int a;\n";
  const std::string_view time_end = "#define a __TIME__";

  const std::string_view date_start =
    "__DATE__\n"
    "int ab;\n";
  const std::string_view date_middle =
    "#define ab __DATE__\n"
    "int ab;\n";
  const std::string_view date_end = "#define ab __DATE__";

  const std::string_view timestamp_start =
    "__TIMESTAMP__\n"
    "int c;\n";
  const std::string_view timestamp_middle =
    "#define c __TIMESTAMP__\n"
    "int c;\n";
  const std::string_view timestamp_end = "#define c __TIMESTAMP__";

  const std::string_view no_temporal =
    "#define ab a__DATE__\n"
    "#define ab  __DATE__a\n"
    "#define ab A__DATE__\n"
    "#define ab  __DATE__A\n"
    "#define ab 0__DATE__\n"
    "#define ab  __DATE__0\n"
    "#define ab _ _DATE__\n"
    "#define ab _ _DATE__\n"
    "#define ab __ DATE__\n"
    "#define ab __D ATE__\n"
    "#define ab __DA TE__\n"
    "#define ab __DAT E__\n"
    "#define ab __DATE __\n"
    "#define ab __DATE_ _\n"
    "#define ab _ _TIME__\n"
    "#define ab __ TIME__\n"
    "#define ab __T IME__\n"
    "#define ab __TI ME__\n"
    "#define ab __TIM E__\n"
    "#define ab __TIME __\n"
    "#define ab __TIME_ _\n";

  const std::string_view temporal_at_avx_boundary =
    "#define alphabet abcdefghijklmnopqrstuvwxyz\n"
    "__DATE__";

  const std::string_view no_temporal_at_avx_boundary =
    "#define alphabet abcdefghijklmnopqrstuvwxyz\n"
    "a__DATE__";

  CHECK(check(time_start).contains(SourceCodeScan::found_time));
  CHECK(check(time_start.substr(1)).empty());

  CHECK(check(time_middle.substr(0)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(1)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(2)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(3)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(4)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(5)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(6)).contains(SourceCodeScan::found_time));
  CHECK(check(time_middle.substr(7)).contains(SourceCodeScan::found_time));

  CHECK(check(time_end).contains(SourceCodeScan::found_time));
  CHECK(check(time_end.substr(time_end.length() - 8))
          .contains(SourceCodeScan::found_time));
  CHECK(check(time_end.substr(time_end.length() - 7)).empty());

  CHECK(check(date_start).contains(SourceCodeScan::found_date));
  CHECK(check(date_start.substr(1)).empty());

  CHECK(check(date_middle.substr(0)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(1)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(2)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(3)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(4)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(5)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(6)).contains(SourceCodeScan::found_date));
  CHECK(check(date_middle.substr(7)).contains(SourceCodeScan::found_date));

  CHECK(check(date_end).contains(SourceCodeScan::found_date));
  CHECK(check(date_end.substr(date_end.length() - 8))
          .contains(SourceCodeScan::found_date));
  CHECK(check(date_end.substr(date_end.length() - 7)).empty());

  CHECK(check(timestamp_start).contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_start.substr(1)).empty());

  CHECK(check(timestamp_middle).contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(1))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(2))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(3))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(4))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(5))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(6))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_middle.substr(7))
          .contains(SourceCodeScan::found_timestamp));

  CHECK(check(timestamp_end).contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_end.substr(timestamp_end.length() - 13))
          .contains(SourceCodeScan::found_timestamp));
  CHECK(check(timestamp_end.substr(timestamp_end.length() - 12)).empty());

  CHECK(check(no_temporal.substr(0)).empty());
  CHECK(check(no_temporal.substr(1)).empty());
  CHECK(check(no_temporal.substr(2)).empty());
  CHECK(check(no_temporal.substr(3)).empty());
  CHECK(check(no_temporal.substr(4)).empty());
  CHECK(check(no_temporal.substr(5)).empty());
  CHECK(check(no_temporal.substr(6)).empty());
  CHECK(check(no_temporal.substr(7)).empty());

  for (size_t i = 0; i < temporal_at_avx_boundary.size() - 8; ++i) {
    CHECK(!check(temporal_at_avx_boundary.substr(i)).empty());
  }
  for (size_t i = 0; i < no_temporal_at_avx_boundary.size() - 8; ++i) {
    CHECK(check(no_temporal_at_avx_boundary.substr(i)).empty());
  }
}

TEST_SUITE_BEGIN("hashutil");

TEST_CASE("hash_command_output_simple")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

  CHECK(hco(h1, "echo", "not used"));
  CHECK(hco(h2, "echo", "not used"));
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_space_removal")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

  CHECK(hco(h1, "echo", "not used"));
  CHECK(hco(h2, " echo ", "not used"));
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_hash_inequality")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

  CHECK(hco(h1, "echo foo", "not used"));
  CHECK(hco(h2, "echo bar", "not used"));
  CHECK(h1.digest() != h2.digest());
}

TEST_CASE("hash_command_output_compiler_substitution")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

  CHECK(hco(h1, "echo foo", "not used"));
#ifdef _WIN32
  REQUIRE(util::write_file("command.bat", "@echo off\r\necho foo\r\n"));
  CHECK(hash_command_output(h2, "%compiler%", "command.bat"));
#else
  CHECK(hash_command_output(h2, "%compiler% foo", "echo"));
#endif
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_stdout_versus_stderr")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

#ifdef _WIN32
  REQUIRE(util::write_file("stderr.bat", "@echo off\r\necho foo>&2\r\n"));
  CHECK(hco(h1, "echo foo", "not used"));
  CHECK(hco(h2, "stderr.bat", "not used"));
#else
  CHECK(hco(h1, "echo foo", "not used"));
  CHECK(hco(h2, "echo foo >&2", "not used"));
#endif
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_multicommand_output")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

#ifdef _WIN32
  h2.hash("foo\r\nbar\r\n");
  REQUIRE(util::write_file("foo.bat", "@echo off\r\necho foo\r\n"));
  REQUIRE(util::write_file("bar.bat", "@echo off\r\necho bar\r\n"));
  CHECK(hash_multicommand_output(h1, "foo.bat; bar.bat", "not used"));
#else
  h2.hash("foo\nbar\n");
  CHECK(hash_multicommand_output(h1, "echo foo; echo bar", "not used"));
  CHECK(h1.digest() == h2.digest());
#endif
}

TEST_CASE("hash_multicommand_output_error_handling")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

  CHECK(!hash_multicommand_output(h2, "false; true", "not used"));
}

TEST_CASE("check_for_temporal_macros")
{
  TestContext test_context;

  SUBCASE("scalar")
  {
    check_temporal_macros(check_for_source_code_patterns_scalar);
  }

#ifdef HAVE_AVX2
  if (util::cpu_supports_avx2()) {
    SUBCASE("avx2")
    {
      check_temporal_macros(check_for_source_code_patterns_avx2);
    }
  }
#endif
}

TEST_CASE("check_for_source_code_patterns: temporal macros and directives")
{
  TestContext test_context;

  const std::string source = FMT(
    "#em{0}bed \"first.bin\"\n"
    "#em{0}bed RESOURCE\n"
    ".inc{0}bin \"second.bin\"\n"
    "const char* build_date = __DATE__;\n"
    "const char* build_time = __TIME__;\n"
    "const char* build_stamp = __TIMESTAMP__;\n",
    "");

  SourceCodeScanResult result;

  SUBCASE("scalar")
  {
    result = check_for_source_code_patterns_scalar(source);
  }

#ifdef HAVE_AVX2
  if (util::cpu_supports_avx2()) {
    SUBCASE("avx2")
    {
      result = check_for_source_code_patterns_avx2(source);
    }
  }
#endif

  CHECK(result.contains(SourceCodeScan::found_embed));
  CHECK(result.contains(SourceCodeScan::found_incbin));
  CHECK(result.contains(SourceCodeScan::found_date));
  CHECK(result.contains(SourceCodeScan::found_time));
  CHECK(result.contains(SourceCodeScan::found_timestamp));
}

TEST_CASE("check_for_source_code_patterns: macro-expanded embed operand")
{
  TestContext test_context;

  const std::string source = FMT("#em{}bed RESOURCE\n", "");
  SourceCodeScanResult result;

  SUBCASE("scalar")
  {
    result = check_for_source_code_patterns_scalar(source);
  }

#ifdef HAVE_AVX2
  if (util::cpu_supports_avx2()) {
    SUBCASE("avx2")
    {
      result = check_for_source_code_patterns_avx2(source);
    }
  }
#endif

  CHECK(result.contains(SourceCodeScan::found_embed));
}

#ifdef HAVE_AVX2

static bool
contains_embed_directive(std::string_view source)
{
  auto result = check_for_source_code_patterns_avx2(source);
  return result.contains(SourceCodeScan::found_embed);
}

static bool
contains_incbin_directive(std::string_view source)
{
  auto result = check_for_source_code_patterns_avx2(source);
  return result.contains(SourceCodeScan::found_incbin);
}

TEST_CASE("check_for_source_code_patterns_avx2: empty source"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK_FALSE(contains_embed_directive(""));
}

TEST_CASE("check_for_source_code_patterns_avx2: no embed directives"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK_FALSE(contains_embed_directive(R"(
#include <stdio.h>
#include "header.h"
int main() { return 0; }
)"));
}

TEST_CASE("check_for_source_code_patterns_avx2: simple quoted embed"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("\n#em{}bed \"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: simple system embed"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("\n#em{}bed <system_data.bin>\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed with path"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(
    FMT("\n#em{}bed \"assets/textures/icon.png\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed with parameters"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(
    contains_embed_directive(FMT("\n#em{}bed \"data.bin\" limit(100)\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed with multiple parameters"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(
    contains_embed_directive(FMT("\n#em{}bed \"data.bin\" prefix(0x00,)"
                                 " suffix(,0x00) if_empty(0) limit(256)\n",
                                 "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: multiple embeds"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(
    contains_embed_directive(FMT("\n#include <stdio.h>\n"
                                 "#em{0}bed \"file1.bin\"\n"
                                 "int main() {{\n"
                                 "#em{0}bed \"file2.bin\"\n"
                                 "#em{0}bed <system.bin>\n"
                                 "  return 0;\n"
                                 "}}\n",
                                 "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed with whitespace"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("\n#  em{}bed   \"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: indented embed"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("\n  #em{}bed \"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed after line continuation"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("#\\\nem{}bed \"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed with line continuation"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("#em{}bed \\\n\"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: embed at start of file"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("#em{}bed \"first.bin\"\n", "")));
}

TEST_CASE(
  "check_for_source_code_patterns_avx2: embed at end of file without newline"
  * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("#em{}bed \"last.bin\"", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: ignores embedded in identifier"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK_FALSE(
    contains_embed_directive("#embedded \"not_this.bin\"\n"
                             "#embedx \"not_this_either.bin\"\n"));
}

TEST_CASE("check_for_source_code_patterns_avx2: handles tabs"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_embed_directive(FMT("#\tem{}bed\t\"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: mixed includes and embeds"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(
    contains_embed_directive(FMT("#include <stdio.h>\n"
                                 "#include \"local.h\"\n"
                                 "#em{0}bed \"binary.dat\"\n"
                                 "#define FOO 1\n"
                                 "#em{0}bed <sys/resource.bin>\n"
                                 "#ifdef BAR\n"
                                 "#em{0}bed \"conditional.bin\"\n"
                                 "#endif\n",
                                 "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: no incbin directive"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK_FALSE(contains_incbin_directive(
    FMT("\n    #include <stdio.h>\n    .inc{}bin data.bin\n  ", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: simple incbin"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_incbin_directive(FMT(".inc{}bin \"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: incbin without space"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_incbin_directive(FMT(".inc{}bin\"data.bin\"\n", "")));
}

TEST_CASE("check_for_source_code_patterns_avx2: escaped quote"
          * doctest::skip(!util::cpu_supports_avx2()))
{
  CHECK(contains_incbin_directive(FMT(".inc{}bin \\\"data.bin\\\"\n", "")));
}

#endif // HAVE_AVX2

TEST_SUITE_END();
