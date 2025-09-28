// Copyright (C) 2010-2024 Joel Rosdahl and other contributors
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

#include <ccache/compopt.hpp>

#include <doctest/doctest.h>

#include <iostream> // for doctest stringification of std::string_view

bool compopt_verify_sortedness_and_flags();

TEST_SUITE_BEGIN("compopt");

TEST_CASE("option_table_should_be_sorted")
{
  CHECK(compopt_verify_sortedness_and_flags());
}

TEST_CASE("affects_cpp_output")
{
  CHECK(compopt_affects_cpp_output("-I"));
  CHECK(!compopt_affects_cpp_output("-Ifoo"));
  CHECK(!compopt_affects_cpp_output("-V"));
  CHECK(!compopt_affects_cpp_output("-doesntexist"));
}

TEST_CASE("affects_compiler_output")
{
  CHECK(compopt_affects_compiler_output("-Xlinker"));
  CHECK(compopt_affects_compiler_output("-all_load"));
  CHECK(!compopt_affects_compiler_output("-U"));
}

TEST_CASE("too_hard")
{
  CHECK(compopt_too_hard("-MM"));
  CHECK(compopt_too_hard("-save-temps"));
  CHECK(compopt_too_hard("-save-temps=cwd"));
  CHECK(compopt_too_hard("-save-temps=obj"));
  CHECK(compopt_too_hard("-analyze"));
  CHECK(compopt_too_hard("--analyzer-output"));
  CHECK(!compopt_too_hard("--analyze"));
  CHECK(!compopt_too_hard("-MD"));
  CHECK(!compopt_too_hard("-fprofile-arcs"));
  CHECK(!compopt_too_hard("-ftest-coverage"));
  CHECK(!compopt_too_hard("-fstack-usage"));
  CHECK(!compopt_too_hard("-fcallgraph-info"));
  CHECK(!compopt_too_hard("-doesntexist"));
}

TEST_CASE("too_hard_for_direct_mode")
{
  CHECK(!compopt_too_hard_for_direct_mode("-nostdinc"));
}

TEST_CASE("compopt_takes_path")
{
  CHECK(compopt_takes_path("-I"));
  CHECK(!compopt_takes_path("-L"));
}

TEST_CASE("compopt_takes_arg")
{
  CHECK(compopt_takes_arg("-Xlinker"));
  CHECK(!compopt_takes_arg("-xxx"));
}

TEST_CASE("prefix_affects_cpp_output")
{
  CHECK(compopt_prefix_affects_cpp_output("-iframework"));
  CHECK(compopt_prefix_affects_cpp_output("-iframework42"));
  CHECK(!compopt_prefix_affects_cpp_output("-iframewor"));
}

TEST_CASE("prefix_affects_compiler_output")
{
  CHECK(compopt_prefix_affects_compiler_output("-Wa,"));
  CHECK(compopt_prefix_affects_compiler_output("-Wa,something"));
  CHECK(!compopt_prefix_affects_compiler_output("-Wa"));
}

TEST_CASE("prefix_takes_path")
{
  CHECK(compopt_prefix_takes_path("-Dfoo") == std::nullopt);
  CHECK(*compopt_prefix_takes_path("-Ifoo") == "foo");
}

TEST_SUITE_END();
