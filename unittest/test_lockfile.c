// Copyright (C) 2010-2020 Joel Rosdahl
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

// This file contains tests for functions in lockfile.c.

#include "../src/ccache.h"
#include "framework.h"
#include "util.h"

TEST_SUITE(lockfile)

TEST(acquire_and_release)
{
	CHECK(lockfile_acquire("test", 1000));

#ifdef _WIN32
	CHECK(path_exists("test.lock"));
#else
	CHECK(is_symlink("test.lock"));
#endif

	lockfile_release("test");
	CHECK(!path_exists("test.lock"));
}

#ifndef _WIN32

TEST(lock_breaking)
{
	char *p;

	CHECK_INT_EQ(0, symlink("foo", "test.lock"));
	CHECK_INT_EQ(0, symlink("foo", "test.lock.lock"));
	CHECK(lockfile_acquire("test", 1000));

	p = x_readlink("test.lock");
	CHECK(p);
	CHECK(!str_eq(p, "foo"));
	CHECK(!path_exists("test.lock.lock"));

	free(p);
}

TEST(failed_lock_breaking)
{
	create_file("test.lock", "");
	CHECK(!lockfile_acquire("test", 1000));
}

#endif // !_WIN32

TEST_SUITE_END
