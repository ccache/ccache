/*
 * Copyright (C) 2010, 2012-2014 Joel Rosdahl
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
 * This file contains tests for functions in util.c.
 */

#include "ccache.h"
#include "test/framework.h"

TEST_SUITE(util)

TEST(basename)
{
	CHECK_STR_EQ_FREE2("foo.c", basename("foo.c"));
	CHECK_STR_EQ_FREE2("foo.c", basename("dir1/dir2/foo.c"));
	CHECK_STR_EQ_FREE2("foo.c", basename("/dir/foo.c"));
	CHECK_STR_EQ_FREE2("", basename("dir1/dir2/"));
}

TEST(dirname)
{
	CHECK_STR_EQ_FREE2(".", dirname("foo.c"));
	CHECK_STR_EQ_FREE2(".", dirname(""));
	CHECK_STR_EQ_FREE2("/", dirname("/"));
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/foo.c"));
	CHECK_STR_EQ_FREE2("/dir", dirname("/dir/foo.c"));
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/"));
}

TEST(common_dir_prefix_length)
{
	CHECK_UNS_EQ(0, common_dir_prefix_length("", ""));
	CHECK_UNS_EQ(0, common_dir_prefix_length("/", "/"));
	CHECK_UNS_EQ(0, common_dir_prefix_length("/", "/b"));
	CHECK_UNS_EQ(0, common_dir_prefix_length("/a", "/b"));
	CHECK_UNS_EQ(2, common_dir_prefix_length("/a", "/a"));
	CHECK_UNS_EQ(2, common_dir_prefix_length("/a", "/a/b"));
	CHECK_UNS_EQ(2, common_dir_prefix_length("/a/b", "/a/c"));
	CHECK_UNS_EQ(4, common_dir_prefix_length("/a/b", "/a/b"));
	CHECK_INT_EQ(2, common_dir_prefix_length("/a/bc", "/a/b"));
	CHECK_INT_EQ(2, common_dir_prefix_length("/a/b", "/a/bc"));
}

TEST(get_relative_path)
{
	CHECK_STR_EQ_FREE2("a", get_relative_path("/doesn't matter", "a"));
	CHECK_STR_EQ_FREE2("a/b", get_relative_path("/doesn't matter", "a/b"));
	CHECK_STR_EQ_FREE2(".", get_relative_path("/a", "/a"));
	CHECK_STR_EQ_FREE2("..", get_relative_path("/a/b", "/a"));
	CHECK_STR_EQ_FREE2("b", get_relative_path("/a", "/a/b"));
	CHECK_STR_EQ_FREE2("b/c", get_relative_path("/a", "/a/b/c"));
	CHECK_STR_EQ_FREE2("../c", get_relative_path("/a/b", "/a/c"));
	CHECK_STR_EQ_FREE2("../c/d", get_relative_path("/a/b", "/a/c/d"));
	CHECK_STR_EQ_FREE2("../../c/d", get_relative_path("/a/b/c", "/a/c/d"));
	CHECK_STR_EQ_FREE2("../..", get_relative_path("/a/b", "/"));
	CHECK_STR_EQ_FREE2("../../c", get_relative_path("/a/b", "/c"));
	CHECK_STR_EQ_FREE2("a/b", get_relative_path("/", "/a/b"));
}

TEST_SUITE_END
