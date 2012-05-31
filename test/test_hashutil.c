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

TEST(hash_source_code_simple_case)
{
	struct mdfour h;
	char input[] = "abc";
	size_t input_len = strlen(input);
	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("a448017aaf21d8525fc10ae87aa6729d-3", hash_result(&h));
}

TEST(hash_source_code_with_c_style_comment)
{
	struct mdfour h;
	char input[] = "a/*b*/c";
	size_t input_len = strlen(input);

	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("1c2c87080ee03418fb1279e3b1f09a68-3", hash_result(&h));

	input[3] = 'd';
	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("1c2c87080ee03418fb1279e3b1f09a68-3", hash_result(&h));
}

TEST(hash_source_code_with_cplusplus_style_comment)
{
	struct mdfour h;
	char input[] = "a//b\nc";
	size_t input_len = strlen(input);

	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("4a3fbbe3c140fa193227dba3814db6e6-3", hash_result(&h));

	input[3] = 'd';
	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("4a3fbbe3c140fa193227dba3814db6e6-3", hash_result(&h));
}

TEST(hash_source_code_with_comment_inside_string)
{
	struct mdfour h;
	char input[] = "a\"//b\"c";
	size_t input_len = strlen(input);

	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("4c2fa74b0843d8f93df5c04c98ccb0a4-7", hash_result(&h));

	input[4] = 'd';
	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("f0069218ec640008cbfa2d150c1061bb-7", hash_result(&h));
}

TEST(hash_source_code_with_quote_in_string)
{
	struct mdfour h;
	char input[] = "a\"\\\"b//c\""; // a"\"b//c"
	size_t input_len = strlen(input);

	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("c4e45e7a7f6f29b000a51f187dc4cf06-9", hash_result(&h));

	hash_start(&h);
	input[7] = 'd';
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("bef8fb852dddcee189b91b068a621c55-9", hash_result(&h));
}

TEST(hash_source_code_with_backslash_at_string_end)
{
	struct mdfour h;
	char input[] = "a\"\\\\\"b//c"; // a"\\"b//c
	size_t input_len = strlen(input);

	hash_start(&h);
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("7f3ccf27edadad1b90cb2cffb59775d6-6", hash_result(&h));

	input[input_len - 1] = 'd';
	hash_source_code_string(&h, input, input_len, "");
	CHECK_STR_EQ_FREE2("7f3ccf27edadad1b90cb2cffb59775d6-6", hash_result(&h));
}

TEST_SUITE_END
