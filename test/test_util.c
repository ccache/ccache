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
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/foo.c"));
	CHECK_STR_EQ_FREE2("/dir", dirname("/dir/foo.c"));
	CHECK_STR_EQ_FREE2("dir1/dir2", dirname("dir1/dir2/"));
}

TEST(format_hash_as_string)
{
	unsigned char hash[16] = {
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"};

	CHECK_STR_EQ_FREE2("00000000000000000000000000000000",
	                   format_hash_as_string(hash, -1));
	CHECK_STR_EQ_FREE2("00000000000000000000000000000000-0",
	                   format_hash_as_string(hash, 0));
	hash[0] = 17;
	hash[15] = 42;
	CHECK_STR_EQ_FREE2("1100000000000000000000000000002a-12345",
	                   format_hash_as_string(hash, 12345));
}

TEST(subst_env_in_string)
{
	char *errmsg;
	const char *shell = getenv("SHELL");

	errmsg = "";
	CHECK_STR_EQ_FREE2(shell,
	                   subst_env_in_string("$SHELL", &errmsg));
	CHECK(!errmsg);

	errmsg = "";
	CHECK_STR_EQ_FREE2("$",
	                   subst_env_in_string("$", &errmsg));
	CHECK(!errmsg);

	errmsg = "";
	CHECK_STR_EQ_FREE12(format("%s %s:%s", shell, shell, shell),
	                    subst_env_in_string("$SHELL $SHELL:$SHELL", &errmsg));
	CHECK(!errmsg);

	errmsg = "";
	CHECK_STR_EQ_FREE12(format("x%s", shell),
	                    subst_env_in_string("x$SHELL", &errmsg));
	CHECK(!errmsg);

	errmsg = "";
	CHECK_STR_EQ_FREE12(format("%sx", shell),
	                    subst_env_in_string("${SHELL}x", &errmsg));
	CHECK(!errmsg);

	CHECK(!subst_env_in_string("$surelydoesntexist", &errmsg));
	CHECK_STR_EQ_FREE2("environment variable \"surelydoesntexist\" not set",
	                   errmsg);

	CHECK(!subst_env_in_string("${SHELL", &errmsg));
	CHECK_STR_EQ_FREE2("syntax error: missing '}' after \"SHELL\"", errmsg);
}

TEST_SUITE_END
