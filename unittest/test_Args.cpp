// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "../src/Args.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

using TestUtil::TestContext;

TEST_CASE("Args default constructor")
{
  Args args;
  CHECK(args.size() == 0);
}

TEST_CASE("Args copy constructor")
{
  Args args1;
  args1.push_back("foo");
  args1.push_back("bar");

  Args args2(args1);
  CHECK(args1 == args2);
}

TEST_CASE("Args move constructor")
{
  Args args1;
  args1.push_back("foo");
  args1.push_back("bar");
  const char* foo_pointer = args1[0].c_str();
  const char* bar_pointer = args1[1].c_str();

  Args args2(std::move(args1));
  CHECK(args1.size() == 0);
  CHECK(args2.size() == 2);
  CHECK(args2[0].c_str() == foo_pointer);
  CHECK(args2[1].c_str() == bar_pointer);
}

TEST_CASE("Args::from_argv")
{
  int argc = 2;
  const char* argv[] = {"a", "b"};
  Args args = Args::from_argv(argc, argv);
  CHECK(args.size() == 2);
  CHECK(args[0] == "a");
  CHECK(args[1] == "b");
}

TEST_CASE("Args::from_string")
{
  Args args = Args::from_string(" c  d\te\r\nf ");
  CHECK(args.size() == 4);
  CHECK(args[0] == "c");
  CHECK(args[1] == "d");
  CHECK(args[2] == "e");
  CHECK(args[3] == "f");
}

TEST_CASE("Args::from_gcc_atfile")
{
  TestContext testContext;

  Args args;

  SECTION("Non-existing file")
  {
    CHECK(Args::from_gcc_atfile("at_file") == nonstd::nullopt);
  }

  SECTION("Empty")
  {
    Util::write_file("at_file", "");
    args = *Args::from_gcc_atfile("at_file");
    CHECK(args.size() == 0);
  }

  SECTION("One argument without newline")
  {
    Util::write_file("at_file", "foo");
    args = *Args::from_gcc_atfile("at_file");
    CHECK(args.size() == 1);
    CHECK(args[0] == "foo");
  }

  SECTION("One argument with newline")
  {
    Util::write_file("at_file", "foo\n");
    args = *Args::from_gcc_atfile("at_file");
    CHECK(args.size() == 1);
    CHECK(args[0] == "foo");
  }

  SECTION("Multiple simple arguments")
  {
    Util::write_file("at_file", "x y z\n");
    args = *Args::from_gcc_atfile("at_file");
    CHECK(args.size() == 3);
    CHECK(args[0] == "x");
    CHECK(args[1] == "y");
    CHECK(args[2] == "z");
  }

  SECTION("Tricky quoting")
  {
    Util::write_file(
      "at_file",
      "first\rsec\\\tond\tthi\\\\rd\nfourth  \tfif\\ th \"si'x\\\" th\""
      " 'seve\nth'\\");
    args = *Args::from_gcc_atfile("at_file");
    CHECK(args.size() == 7);
    CHECK(args[0] == "first");
    CHECK(args[1] == "sec\tond");
    CHECK(args[2] == "thi\\rd");
    CHECK(args[3] == "fourth");
    CHECK(args[4] == "fif th");
    CHECK(args[5] == "si'x\" th");
    CHECK(args[6] == "seve\nth");
  }
}

TEST_CASE("Args copy assignment operator")
{
  Args args1 = Args::from_string("x y");
  Args args2;
  args2 = args1;
  CHECK(args2.size() == 2);
  CHECK(args2[0] == "x");
  CHECK(args2[1] == "y");
}

TEST_CASE("Args move assignment operator")
{
  Args args1 = Args::from_string("x y");
  const char* x_pointer = args1[0].c_str();
  const char* y_pointer = args1[1].c_str();

  Args args2;
  args2 = std::move(args1);
  CHECK(args1.size() == 0);
  CHECK(args2.size() == 2);
  CHECK(args2[0].c_str() == x_pointer);
  CHECK(args2[1] == y_pointer);
}

TEST_CASE("Args equality operators")
{
  Args args1 = Args::from_string("x y");
  Args args2 = Args::from_string("x y");
  Args args3 = Args::from_string("y x");
  CHECK(args1 == args1);
  CHECK(args1 == args2);
  CHECK(args2 == args1);
  CHECK(args1 != args3);
  CHECK(args3 != args1);
}

TEST_CASE("Args::empty")
{
  Args args;
  CHECK(args.empty());
  args.push_back("1");
  CHECK(!args.empty());
}

TEST_CASE("Args::size")
{
  Args args;
  CHECK(args.size() == 0);
  args.push_back("1");
  CHECK(args.size() == 1);
  args.push_back("2");
  CHECK(args.size() == 2);
}

TEST_CASE("Args indexing")
{
  const Args args1 = Args::from_string("1 2 3");
  CHECK(args1[0] == "1");
  CHECK(args1[1] == "2");
  CHECK(args1[2] == "3");

  Args args2 = Args::from_string("1 2 3");
  CHECK(args2[0] == "1");
  CHECK(args2[1] == "2");
  CHECK(args2[2] == "3");
}

TEST_CASE("Args::to_argv")
{
  Args args = Args::from_string("1 2 3");
  auto argv = args.to_argv();
  CHECK(std::string(argv[0]) == "1");
  CHECK(std::string(argv[1]) == "2");
  CHECK(std::string(argv[2]) == "3");
  CHECK(argv[3] == nullptr);
}

TEST_CASE("Args::to_string")
{
  CHECK(Args::from_string("a little string").to_string() == "a little string");
}

TEST_CASE("Args operations")
{
  Args args = Args::from_string("eeny meeny miny moe");
  Args more_args = Args::from_string("x y");
  Args no_args;

  SECTION("erase_with_prefix")
  {
    args.erase_with_prefix("m");
    CHECK(args == Args::from_string("eeny"));
  }

  SECTION("insert empty args")
  {
    args.insert(2, no_args);
    CHECK(args == Args::from_string("eeny meeny miny moe"));
  }

  SECTION("insert non-empty args")
  {
    args.insert(4, more_args);
    args.insert(2, more_args);
    args.insert(0, more_args);
    CHECK(args == Args::from_string("x y eeny meeny x y miny moe x y"));
  }

  SECTION("pop_back")
  {
    args.pop_back();
    CHECK(args == Args::from_string("eeny meeny miny"));

    args.pop_back(2);
    CHECK(args == Args::from_string("eeny"));
  }

  SECTION("pop_front")
  {
    args.pop_front();
    CHECK(args == Args::from_string("meeny miny moe"));

    args.pop_front(2);
    CHECK(args == Args::from_string("moe"));
  }

  SECTION("push_back string")
  {
    args.push_back("foo");
    CHECK(args == Args::from_string("eeny meeny miny moe foo"));
  }

  SECTION("push_back args")
  {
    args.push_back(more_args);
    CHECK(args == Args::from_string("eeny meeny miny moe x y"));
  }

  SECTION("push_front string")
  {
    args.push_front("foo");
    CHECK(args == Args::from_string("foo eeny meeny miny moe"));
  }

  SECTION("replace")
  {
    args.replace(3, more_args);
    args.replace(2, no_args);
    args.replace(0, more_args);
    CHECK(args == Args::from_string("x y meeny x y"));
  }
}
