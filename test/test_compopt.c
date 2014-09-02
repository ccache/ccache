/*
 * Copyright (C) 2010, 2012 Joel Rosdahl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * This file contains tests for the compopt_* functions.
 */

#include "ccache.h"
#include "compopt.h"
#include "test/framework.h"

TEST_SUITE(compopt)

TEST(option_table_should_be_sorted)
{
	bool compopt_verify_sortedness();
	CHECK(compopt_verify_sortedness());
}

TEST(dash_I_affects_cpp)
{
	CHECK(compopt_affects_cpp("-I"));
	CHECK(!compopt_affects_cpp("-Ifoo"));
}

TEST(compopt_short)
{
	CHECK(compopt_short(compopt_affects_cpp, "-Ifoo"));
	CHECK(!compopt_short(compopt_affects_cpp, "-include"));
}

TEST(dash_V_doesnt_affect_cpp)
{
	CHECK(!compopt_affects_cpp("-V"));
}

TEST(dash_doesnexist_doesnt_affect_cpp)
{
	CHECK(!compopt_affects_cpp("-doesntexist"));
}

TEST(dash_MM_too_hard)
{
	CHECK(compopt_too_hard("-MM"));
}

TEST(dash_MD_not_too_hard)
{
	CHECK(!compopt_too_hard("-MD"));
}

TEST(dash_fprofile_arcs_not_too_hard)
{
	CHECK(!compopt_too_hard("-fprofile-arcs"));
}

TEST(dash_ftest_coverage_not_too_hard)
{
	CHECK(!compopt_too_hard("-ftest-coverage"));
}

TEST(dash_doesnexist_not_too_hard)
{
	CHECK(!compopt_too_hard("-doesntexist"));
}

TEST(dash_Xpreprocessor_too_hard_for_direct_mode)
{
	CHECK(compopt_too_hard_for_direct_mode("-Xpreprocessor"));
}

TEST(dash_nostdinc_not_too_hard_for_direct_mode)
{
	CHECK(!compopt_too_hard_for_direct_mode("-nostdinc"));
}

TEST(dash_I_takes_path)
{
	CHECK(compopt_takes_path("-I"));
}

TEST(dash_Xlinker_takes_arg)
{
	CHECK(compopt_takes_arg("-Xlinker"));
}

TEST(dash_xxx_doesnt_take_arg)
{
	CHECK(!compopt_takes_arg("-xxx"));
}

TEST(dash_iframework_prefix_affects_cpp)
{
	CHECK(compopt_prefix_affects_cpp("-iframework"));
}

TEST_SUITE_END
