// Copyright (C) 2010-2016 Joel Rosdahl
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

// This file contains tests for the functions operating on struct args.

#include "../ccache.h"
#include "framework.h"
#include "util.h"

TEST_SUITE(args)

TEST(args_init_empty)
{
	struct args *args = args_init(0, NULL);
	CHECK(args);
	CHECK_INT_EQ(0, args->argc);
	CHECK(!args->argv[0]);
	args_free(args);
}

TEST(args_init_populated)
{
	char *argv[] = {"first", "second"};
	struct args *args = args_init(2, argv);
	CHECK(args);
	CHECK_INT_EQ(2, args->argc);
	CHECK_STR_EQ("first", args->argv[0]);
	CHECK_STR_EQ("second", args->argv[1]);
	CHECK(!args->argv[2]);
	args_free(args);
}

TEST(args_init_from_string)
{
	struct args *args = args_init_from_string("first second\tthird\nfourth");
	CHECK(args);
	CHECK_INT_EQ(4, args->argc);
	CHECK_STR_EQ("first", args->argv[0]);
	CHECK_STR_EQ("second", args->argv[1]);
	CHECK_STR_EQ("third", args->argv[2]);
	CHECK_STR_EQ("fourth", args->argv[3]);
	CHECK(!args->argv[4]);
	args_free(args);
}

TEST(args_init_from_gcc_atfile)
{
	struct args *args;
	const char *argtext =
#ifdef _WIN32
	// On windows, we need to keep any \ that are directory delimiter.
	// So use quotes to put space in arguments.
	  "first\r'sec\tond'\tthi\\rd\nfourth  \t\"fif th\" \"si'x th\""
	  " 'seve\nth'";
#else
	  "first\rsec\\\tond\tthi\\\\rd\nfourth  \tfif\\ th \"si'x\\\" th\""
	  " 'seve\nth'\\";
#endif

	create_file("gcc_atfile", argtext);

	args = args_init_from_gcc_atfile("gcc_atfile");
	CHECK(args);
	CHECK_INT_EQ(7, args->argc);
	CHECK_STR_EQ("first", args->argv[0]);
	CHECK_STR_EQ("sec\tond", args->argv[1]);
	CHECK_STR_EQ("thi\\rd", args->argv[2]);
	CHECK_STR_EQ("fourth", args->argv[3]);
	CHECK_STR_EQ("fif th", args->argv[4]);
#ifndef _WIN32
	CHECK_STR_EQ("si'x\" th", args->argv[5]);
	CHECK_STR_EQ("seve\nth", args->argv[6]);
#else
	CHECK_STR_EQ("si'x th", args->argv[5]);
	CHECK_STR_EQ("seve\r\nth", args->argv[6]);
#endif
	CHECK(!args->argv[7]);
	args_free(args);
}

TEST(args_copy)
{
	struct args *args1 = args_init_from_string("foo");
	struct args *args2 = args_copy(args1);
	CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_add)
{
	struct args *args = args_init_from_string("first");
	CHECK_INT_EQ(1, args->argc);
	args_add(args, "second");
	CHECK_INT_EQ(2, args->argc);
	CHECK_STR_EQ("second", args->argv[1]);
	CHECK(!args->argv[2]);
	args_free(args);
}

TEST(args_extend)
{
	struct args *args1 = args_init_from_string("first");
	struct args *args2 = args_init_from_string("second third");
	CHECK_INT_EQ(1, args1->argc);
	args_extend(args1, args2);
	CHECK_INT_EQ(3, args1->argc);
	CHECK_STR_EQ("second", args1->argv[1]);
	CHECK_STR_EQ("third", args1->argv[2]);
	CHECK(!args1->argv[3]);
	args_free(args1);
	args_free(args2);
}

TEST(args_pop)
{
	struct args *args = args_init_from_string("first second third");
	args_pop(args, 2);
	CHECK_INT_EQ(1, args->argc);
	CHECK_STR_EQ("first", args->argv[0]);
	CHECK(!args->argv[1]);
	args_free(args);
}

TEST(args_set)
{
	struct args *args = args_init_from_string("first second third");
	args_set(args, 1, "2nd");
	CHECK_INT_EQ(3, args->argc);
	CHECK_STR_EQ("first", args->argv[0]);
	CHECK_STR_EQ("2nd", args->argv[1]);
	CHECK_STR_EQ("third", args->argv[2]);
	CHECK(!args->argv[3]);
	args_free(args);
}

TEST(args_remove_first)
{
	struct args *args1 = args_init_from_string("first second third");
	struct args *args2 = args_init_from_string("second third");
	args_remove_first(args1);
	CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_add_prefix)
{
	struct args *args1 = args_init_from_string("second third");
	struct args *args2 = args_init_from_string("first second third");
	args_add_prefix(args1, "first");
	CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_strip)
{
	struct args *args1 = args_init_from_string("first xsecond third xfourth");
	struct args *args2 = args_init_from_string("first third");
	args_strip(args1, "x");
	CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_to_string)
{
	struct args *args = args_init_from_string("first second");
	CHECK_STR_EQ_FREE2("first second", args_to_string(args));
	args_free(args);
}

TEST(args_insert)
{
	struct args *args = args_init_from_string("first second third fourth fifth");

	struct args *src1 = args_init_from_string("alpha beta gamma");
	struct args *src2 = args_init_from_string("one");
	struct args *src3 = args_init_from_string("");
	struct args *src4 = args_init_from_string("alpha beta gamma");
	struct args *src5 = args_init_from_string("one");
	struct args *src6 = args_init_from_string("");

	args_insert(args, 2, src1, true);
	CHECK_STR_EQ_FREE2("first second alpha beta gamma fourth fifth",
					   args_to_string(args));
	CHECK_INT_EQ(7, args->argc);
	args_insert(args, 2, src2, true);
	CHECK_STR_EQ_FREE2("first second one beta gamma fourth fifth",
					   args_to_string(args));
	CHECK_INT_EQ(7, args->argc);
	args_insert(args, 2, src3, true);
	CHECK_STR_EQ_FREE2("first second beta gamma fourth fifth",
					   args_to_string(args));
	CHECK_INT_EQ(6, args->argc);

	args_insert(args, 1, src4, false);
	CHECK_STR_EQ_FREE2("first alpha beta gamma second beta gamma fourth fifth",
					   args_to_string(args));
	CHECK_INT_EQ(9, args->argc);
	args_insert(args, 1, src5, false);
	CHECK_STR_EQ_FREE2(
	  "first one alpha beta gamma second beta gamma fourth fifth",
	  args_to_string(args));
	CHECK_INT_EQ(10, args->argc);
	args_insert(args, 1, src6, false);
	CHECK_STR_EQ_FREE2(
	  "first one alpha beta gamma second beta gamma fourth fifth",
	  args_to_string(args));
	CHECK_INT_EQ(10, args->argc);

	args_free(args);
}

TEST_SUITE_END
