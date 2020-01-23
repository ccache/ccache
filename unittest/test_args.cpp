// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

// This file contains tests for the functions operating on struct args.

#include "../src/args.hpp"
#include "framework.hpp"
#include "util.hpp"

TEST_SUITE(args)

TEST(args_init_empty)
{
  Args args;
  CHECK_INT_EQ(0, args->argc);
  CHECK(!args->argv[0]);
  args_free(args);
}

TEST(args_init_populated)
{
  const char* argv[] = {"first", "second"};
  Args args = args_init(2, argv);
  CHECK_INT_EQ(2, args->argc);
  CHECK_STR_EQ("first", args->argv[0]);
  CHECK_STR_EQ("second", args->argv[1]);
  args_free(args);
}

TEST(args_init_from_string)
{
  Args args = args_init_from_string("first second\tthird\nfourth");
  CHECK_INT_EQ(4, args->argc);
  CHECK_STR_EQ("first", args->argv[0]);
  CHECK_STR_EQ("second", args->argv[1]);
  CHECK_STR_EQ("third", args->argv[2]);
  CHECK_STR_EQ("fourth", args->argv[3]);
  args_free(args);
}

TEST(args_init_from_gcc_atfile)
{
  const char* argtext =
    "first\rsec\\\tond\tthi\\\\rd\nfourth  \tfif\\ th \"si'x\\\" th\""
    " 'seve\nth'\\";

  create_file("gcc_atfile", argtext);

  auto args = args_init_from_gcc_atfile("gcc_atfile");
  CHECK(args);
  CHECK_INT_EQ(7, args->argc);
  CHECK_STR_EQ("first", args->argv[0]);
  CHECK_STR_EQ("sec\tond", args->argv[1]);
  CHECK_STR_EQ("thi\\rd", args->argv[2]);
  CHECK_STR_EQ("fourth", args->argv[3]);
  CHECK_STR_EQ("fif th", args->argv[4]);
  CHECK_STR_EQ("si'x\" th", args->argv[5]);
#ifndef _WIN32
  CHECK_STR_EQ("seve\nth", args->argv[6]);
#else
  CHECK_STR_EQ("seve\r\nth", args->argv[6]);
#endif
  CHECK(!args->argv[7]);
  args_free(*args);
}

TEST(args_copy)
{
  Args args1 = args_init_from_string("foo");
  Args args2 = args_copy(args1);
  CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_add)
{
  Args args = args_init_from_string("first");
  CHECK_INT_EQ(1, args->argc);
  args_add(args, "second");
  CHECK_INT_EQ(2, args->argc);
  CHECK_STR_EQ("second", args->argv[1]);
  CHECK(!args->argv[2]);
  args_free(args);
}

TEST(args_extend)
{
  Args args1 = args_init_from_string("first");
  Args args2 = args_init_from_string("second third");
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
  Args args = args_init_from_string("first second third");
  args_pop(args, 2);
  CHECK_INT_EQ(1, args->argc);
  CHECK_STR_EQ("first", args->argv[0]);
  CHECK(!args->argv[1]);
  args_free(args);
}

TEST(args_set)
{
  Args args = args_init_from_string("first second third");
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
  Args args1 = args_init_from_string("first second third");
  Args args2 = args_init_from_string("second third");
  args_remove_first(args1);
  CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_add_prefix)
{
  Args args1 = args_init_from_string("second third");
  Args args2 = args_init_from_string("first second third");
  args_add_prefix(args1, "first");
  CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_strip)
{
  Args args1 = args_init_from_string("first xsecond third xfourth");
  Args args2 = args_init_from_string("first third");
  args_strip(args1, "x");
  CHECK_ARGS_EQ_FREE12(args1, args2);
}

TEST(args_to_string)
{
  Args args = args_init_from_string("first second");
  CHECK_STR_EQ_FREE2("first second", args_to_string(args));
  args_free(args);
}

TEST(args_insert)
{
  Args args = args_init_from_string("first second third fourth fifth");

  Args src1 = args_init_from_string("alpha beta gamma");
  Args src2 = args_init_from_string("one");
  Args src3 = args_init_from_string("");
  Args src4 = args_init_from_string("alpha beta gamma");
  Args src5 = args_init_from_string("one");
  Args src6 = args_init_from_string("");

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
