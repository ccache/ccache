// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "../src/compopt.hpp"

#include "third_party/catch.hpp"

bool compopt_verify_sortedness_and_flags();

TEST_CASE("option_table_should_be_sorted")
{
  CHECK(compopt_verify_sortedness_and_flags());
}

TEST_CASE("dash_I_affects_cpp")
{
  CHECK(compopt_affects_cpp("-I"));
  CHECK(!compopt_affects_cpp("-Ifoo"));
}

TEST_CASE("compopt_short")
{
  CHECK(compopt_short(compopt_affects_cpp, "-Ifoo"));
  CHECK(!compopt_short(compopt_affects_cpp, "-include"));
}

TEST_CASE("dash_V_doesnt_affect_cpp")
{
  CHECK(!compopt_affects_cpp("-V"));
}

TEST_CASE("dash_doesntexist_doesnt_affect_cpp")
{
  CHECK(!compopt_affects_cpp("-doesntexist"));
}

TEST_CASE("dash_MM_too_hard")
{
  CHECK(compopt_too_hard("-MM"));
}

TEST_CASE("dash_save_temps_too_hard")
{
  CHECK(compopt_too_hard("-save-temps"));
}

TEST_CASE("dash_save_temps_cwd_too_hard")
{
  CHECK(compopt_too_hard("-save-temps=cwd"));
}

TEST_CASE("dash_save_temps_obj_too_hard")
{
  CHECK(compopt_too_hard("-save-temps=obj"));
}

TEST_CASE("dash_MD_not_too_hard")
{
  CHECK(!compopt_too_hard("-MD"));
}

TEST_CASE("dash_fprofile_arcs_not_too_hard")
{
  CHECK(!compopt_too_hard("-fprofile-arcs"));
}

TEST_CASE("dash_ftest_coverage_not_too_hard")
{
  CHECK(!compopt_too_hard("-ftest-coverage"));
}

TEST_CASE("dash_fstack_usage_not_too_hard")
{
  CHECK(!compopt_too_hard("-fstack-usage"));
}

TEST_CASE("dash_doesntexist_not_too_hard")
{
  CHECK(!compopt_too_hard("-doesntexist"));
}

TEST_CASE("dash_Xpreprocessor_too_hard_for_direct_mode")
{
  CHECK(compopt_too_hard_for_direct_mode("-Xpreprocessor"));
}

TEST_CASE("dash_nostdinc_not_too_hard_for_direct_mode")
{
  CHECK(!compopt_too_hard_for_direct_mode("-nostdinc"));
}

TEST_CASE("dash_I_takes_path")
{
  CHECK(compopt_takes_path("-I"));
}

TEST_CASE("dash_Xlinker_takes_arg")
{
  CHECK(compopt_takes_arg("-Xlinker"));
}

TEST_CASE("dash_xxx_doesnt_take_arg")
{
  CHECK(!compopt_takes_arg("-xxx"));
}

TEST_CASE("dash_iframework_prefix_affects_cpp")
{
  CHECK(compopt_prefix_affects_cpp("-iframework"));
}

TEST_CASE("dash_analyze_too_hard")
{
  CHECK(compopt_too_hard("-analyze"));
}

TEST_CASE("dash_dash_analyze_too_hard")
{
  CHECK(compopt_too_hard("--analyze"));
}
