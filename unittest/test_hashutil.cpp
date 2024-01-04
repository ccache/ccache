// Copyright (C) 2010-2023 Joel Rosdahl and other contributors
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

#include "../src/Hash.hpp"
#include "../src/hashutil.hpp"
#include "TestUtil.hpp"

#include <util/file.hpp>

#include "third_party/doctest.h"

#include <sys/stat.h>

using TestUtil::TestContext;

TEST_SUITE_BEGIN("hashutil");

TEST_CASE("hash_command_output_simple")
{
  Hash h1;
  Hash h2;

  CHECK(hash_command_output(h1, "echo", "not used"));
  CHECK(hash_command_output(h2, "echo", "not used"));
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_space_removal")
{
  Hash h1;
  Hash h2;

  CHECK(hash_command_output(h1, "echo", "not used"));
  CHECK(hash_command_output(h2, " echo ", "not used"));
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_hash_inequality")
{
  Hash h1;
  Hash h2;

  CHECK(hash_command_output(h1, "echo foo", "not used"));
  CHECK(hash_command_output(h2, "echo bar", "not used"));
  CHECK(h1.digest() != h2.digest());
}

TEST_CASE("hash_command_output_compiler_substitution")
{
  Hash h1;
  Hash h2;

  CHECK(hash_command_output(h1, "echo foo", "not used"));
  CHECK(hash_command_output(h2, "%compiler% foo", "echo"));
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_command_output_stdout_versus_stderr")
{
  TestContext test_context;

  Hash h1;
  Hash h2;

#ifndef _WIN32
  util::write_file("stderr.sh", "#!/bin/sh\necho foo >&2\n");
  chmod("stderr.sh", 0555);
  CHECK(hash_command_output(h1, "echo foo", "not used"));
  CHECK(hash_command_output(h2, "./stderr.sh", "not used"));
#else
  util::write_file("stderr.bat", "@echo off\r\necho foo>&2\r\n");
  CHECK(hash_command_output(h1, "echo foo", "not used"));
  CHECK(hash_command_output(h2, "stderr.bat", "not used"));
#endif
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_multicommand_output")
{
  Hash h1;
  Hash h2;

#ifndef _WIN32
  util::write_file("foo.sh", "#!/bin/sh\necho foo\necho bar\n");
  chmod("foo.sh", 0555);
  CHECK(hash_multicommand_output(h2, "echo foo; echo bar", "not used"));
  CHECK(hash_multicommand_output(h1, "./foo.sh", "not used"));
#else
  util::write_file("foo.bat", "@echo off\r\necho foo\r\necho bar\r\n");
  CHECK(hash_multicommand_output(h2, "echo foo; echo bar", "not used"));
  CHECK(hash_multicommand_output(h1, "foo.bat", "not used"));
#endif
  CHECK(h1.digest() == h2.digest());
}

TEST_CASE("hash_multicommand_output_error_handling")
{
  Hash h1;
  Hash h2;

  CHECK(!hash_multicommand_output(h2, "false; true", "not used"));
}

TEST_CASE("check_for_temporal_macros")
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

  auto check = check_for_temporal_macros;

  CHECK(check(time_start).contains(HashSourceCode::found_time));
  CHECK(check(time_start.substr(1)).empty());

  CHECK(check(time_middle.substr(0)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(1)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(2)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(3)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(4)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(5)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(6)).contains(HashSourceCode::found_time));
  CHECK(check(time_middle.substr(7)).contains(HashSourceCode::found_time));

  CHECK(check(time_end).contains(HashSourceCode::found_time));
  CHECK(check(time_end.substr(time_end.length() - 8))
          .contains(HashSourceCode::found_time));
  CHECK(check(time_end.substr(time_end.length() - 7)).empty());

  CHECK(check(date_start).contains(HashSourceCode::found_date));
  CHECK(check(date_start.substr(1)).empty());

  CHECK(check(date_middle.substr(0)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(1)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(2)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(3)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(4)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(5)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(6)).contains(HashSourceCode::found_date));
  CHECK(check(date_middle.substr(7)).contains(HashSourceCode::found_date));

  CHECK(check(date_end).contains(HashSourceCode::found_date));
  CHECK(check(date_end.substr(date_end.length() - 8))
          .contains(HashSourceCode::found_date));
  CHECK(check(date_end.substr(date_end.length() - 7)).empty());

  CHECK(check(timestamp_start).contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_start.substr(1)).empty());

  CHECK(check(timestamp_middle).contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(1))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(2))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(3))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(4))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(5))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(6))
          .contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_middle.substr(7))
          .contains(HashSourceCode::found_timestamp));

  CHECK(check(timestamp_end).contains(HashSourceCode::found_timestamp));
  CHECK(check(timestamp_end.substr(timestamp_end.length() - 13))
          .contains(HashSourceCode::found_timestamp));
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

TEST_SUITE_END();
