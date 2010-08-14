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
 * This file contains tests for functions in hashutil.c.
 */

#include "ccache.h"
#include "hashutil.h"
#include "test/framework.h"
#include "test/util.h"

TEST_SUITE(hashutil)

TEST(hash_command_output_simple)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	CHECK(hash_command_output(&h1, "echo", "not used"));
	CHECK(hash_command_output(&h2, "echo", "not used"));
	CHECK(hash_equal(&h1, &h2));
}

TEST(hash_command_output_space_removal)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	CHECK(hash_command_output(&h1, "echo", "not used"));
	CHECK(hash_command_output(&h2, " echo ", "not used"));
	CHECK(hash_equal(&h1, &h2));
}

TEST(hash_command_output_hash_inequality)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	CHECK(hash_command_output(&h1, "echo foo", "not used"));
	CHECK(hash_command_output(&h2, "echo bar", "not used"));
	CHECK(!hash_equal(&h1, &h2));
}

TEST(hash_command_output_compiler_substitution)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	CHECK(hash_command_output(&h1, "echo foo", "not used"));
	CHECK(hash_command_output(&h2, "%compiler% foo", "echo"));
	CHECK(hash_equal(&h1, &h2));
}

TEST(hash_command_output_stdout_versus_stderr)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	create_file("stderr.sh", "#!/bin/sh\necho foo >&2\n");
	chmod("stderr.sh", 0555);
	CHECK(hash_command_output(&h1, "echo foo", "not used"));
	CHECK(hash_command_output(&h2, "./stderr.sh", "not used"));
	CHECK(hash_equal(&h1, &h2));
}

TEST(hash_multicommand_output)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	create_file("foo.sh", "#!/bin/sh\necho foo\necho bar\n");
	chmod("foo.sh", 0555);
	CHECK(hash_multicommand_output(&h2, "echo foo; echo bar", "not used"));
	CHECK(hash_multicommand_output(&h1, "./foo.sh", "not used"));
	CHECK(hash_equal(&h1, &h2));
}

TEST(hash_multicommand_output_error_handling)
{
	struct mdfour h1, h2;
	hash_start(&h1);
	hash_start(&h2);
	CHECK(!hash_multicommand_output(&h2, "false; true", "not used"));
}

TEST_SUITE_END
