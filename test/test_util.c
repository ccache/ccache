/*
 * Copyright (C) 2010 Joel Rosdahl
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

#include "../ccache.h"
#include "framework.h"

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
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/foo.c"));
	CHECK_STR_EQ_FREE2("/dir", dirname("/dir/foo.c"));
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/"));
}

TEST_SUITE_END
