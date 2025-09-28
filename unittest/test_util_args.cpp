// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "testutil.hpp"

#include <ccache/config.hpp>
#include <ccache/util/args.hpp>
#include <ccache/util/file.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("Args");

using TestUtil::TestContext;
using util::Args;

TEST_CASE("Args default constructor")
{
  Args args;
  CHECK(args.size() == 0);
}

TEST_CASE("Args initializer list constructor")
{
  Args args{"foo", "bar"};
  CHECK(args.size() == 2);
  CHECK(args[0] == "foo");
  CHECK(args[1] == "bar");
}

TEST_CASE("Args copy constructor")
{
  Args args1{"foo", "bar"};
  Args args2(args1);
  CHECK(args1 == args2);
}

TEST_CASE("Args move constructor")
{
  Args args1{"foo", "bar"};
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

TEST_CASE("Args::from_response_file")
{
  TestContext test_context;
  using ResponseFileFormat = Args::ResponseFileFormat;

  Args args;

  SUBCASE("Nonexistent file")
  {
    CHECK(Args::from_response_file("rsp_file", ResponseFileFormat::posix)
          == std::nullopt);
  }

  SUBCASE("Empty")
  {
    REQUIRE(util::write_file("rsp_file", ""));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::posix);
    CHECK(args.size() == 0);
  }

  SUBCASE("One argument without newline")
  {
    REQUIRE(util::write_file("rsp_file", "foo"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::posix);
    CHECK(args.size() == 1);
    CHECK(args[0] == "foo");
  }

  SUBCASE("One argument with newline")
  {
    REQUIRE(util::write_file("rsp_file", "foo\n"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::posix);
    CHECK(args.size() == 1);
    CHECK(args[0] == "foo");
  }

  SUBCASE("Multiple simple arguments")
  {
    REQUIRE(util::write_file("rsp_file", "x y z\n"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::posix);
    CHECK(args.size() == 3);
    CHECK(args[0] == "x");
    CHECK(args[1] == "y");
    CHECK(args[2] == "z");
  }

  SUBCASE("Tricky quoting")
  {
    REQUIRE(util::write_file(
      "rsp_file",
      "first\rsec\\\tond\tthi\\\\rd\nfourth  \tfif\\ th \"si'x\\\" th\""
      " 'seve\nth'\\"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::posix);
    CHECK(args.size() == 7);
    CHECK(args[0] == "first");
    CHECK(args[1] == "sec\tond");
    CHECK(args[2] == "thi\\rd");
    CHECK(args[3] == "fourth");
    CHECK(args[4] == "fif th");
    CHECK(args[5] == "si'x\" th");
    CHECK(args[6] == "seve\nth");
  }

  SUBCASE("Ignore single quote in MSVC format")
  {
    REQUIRE(util::write_file("rsp_file", "'a b'"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 2);
    CHECK(args[0] == "'a");
    CHECK(args[1] == "b'");
  }

  SUBCASE("Backslash as directory separator in MSVC format")
  {
    REQUIRE(util::write_file("rsp_file", R"("-DDIRSEP='A\B\C'")"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 1);
    CHECK(args[0] == R"(-DDIRSEP='A\B\C')");
  }

  SUBCASE("Backslash before quote in MSVC format")
  {
    REQUIRE(util::write_file("rsp_file", R"(/Fo"N.dir\Release\\")"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 1);
    CHECK(args[0] == R"(/FoN.dir\Release\)");
  }

  SUBCASE("Arguments on multiple lines in MSVC format")
  {
    REQUIRE(util::write_file("rsp_file", "a\nb"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 2);
    CHECK(args[0] == "a");
    CHECK(args[1] == "b");
  }

  SUBCASE("Tricky quoting in MSVC format (#1247)")
  {
    REQUIRE(util::write_file(
      "rsp_file",
      R"(\ \\ '\\' "\\" '"\\"' "'\\'" '''\\''' ''"\\"'' '"'\\'"' '""\\""' "''\\''" "'"\\"'" ""'\\'"" """\\""" )"
      R"(\'\' '\'\'' "\'\'" ''\'\''' '"\'\'"' "'\'\''" ""\'\'"" '''\'\'''' ''"\'\'"'' '"'\'\''"' '""\'\'""' "''\'\'''" "'"\'\'"'" ""'\'\''"" """\'\'""" )"
      R"(\"\" '\"\"' "\"\"" ''\"\"'' '"\"\""' "'\"\"'" ""\"\""" '''\"\"''' ''"\"\""'' '"'\"\"'"' '""\"\"""' "''\"\"''" "'"\"\""'" ""'\"\"'"" """\"\"""")"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 44);
    CHECK(args[0] == R"(\)");
    CHECK(args[1] == R"(\\)");
    CHECK(args[2] == R"('\\')");
    CHECK(args[3] == R"(\)");
    CHECK(args[4] == R"('\')");
    CHECK(args[5] == R"('\\')");
    CHECK(args[6] == R"('''\\''')");
    CHECK(args[7] == R"(''\'')");
    CHECK(args[8] == R"(''\\'')");
    CHECK(args[9] == R"('\')");
    CHECK(args[10] == R"(''\\'')");
    CHECK(args[11] == R"('\')");
    CHECK(args[12] == R"('\\')");
    CHECK(args[13] == R"("\")");
    CHECK(args[14] == R"(\'\')");
    CHECK(args[15] == R"('\'\'')");
    CHECK(args[16] == R"(\'\')");
    CHECK(args[17] == R"(''\'\''')");
    CHECK(args[18] == R"('\'\'')");
    CHECK(args[19] == R"('\'\'')");
    CHECK(args[20] == R"(\'\')");
    CHECK(args[21] == R"('''\'\'''')");
    CHECK(args[22] == R"(''\'\''')");
    CHECK(args[23] == R"(''\'\''')");
    CHECK(args[24] == R"('\'\'')");
    CHECK(args[25] == R"(''\'\''')");
    CHECK(args[26] == R"('\'\'')");
    CHECK(args[27] == R"('\'\'')");
    CHECK(args[28] == R"("\'\'")");
    CHECK(args[29] == R"("")");
    CHECK(args[30] == R"('""')");
    CHECK(args[31] == R"("")");
    CHECK(args[32] == R"(''""'')");
    CHECK(args[33] == R"('""')");
    CHECK(args[34] == R"('""')");
    CHECK(args[35] == R"("")");
    CHECK(args[36] == R"('''""''')");
    CHECK(args[37] == R"(''""'')");
    CHECK(args[38] == R"(''""'')");
    CHECK(args[39] == R"('""')");
    CHECK(args[40] == R"(''""'')");
    CHECK(args[41] == R"('""')");
    CHECK(args[42] == R"('""')");
    CHECK(args[43] == R"("""")");
  }

  SUBCASE("Quoting from Microsoft documentation in MSVC format")
  {
    // See
    // https://learn.microsoft.com/en-us/previous-versions//17w5ykft(v=vs.85)?redirectedfrom=MSDN
    REQUIRE(util::write_file("rsp_file",
                             R"("abc" d e )"
                             R"(a\\\b d"e f"g h )"
                             R"(a\\\"b c d )"
                             R"(a\\\\"b c" d e)"));
    args = *Args::from_response_file("rsp_file", ResponseFileFormat::windows);
    CHECK(args.size() == 12);
    CHECK(args[0] == R"(abc)");
    CHECK(args[1] == R"(d)");
    CHECK(args[2] == R"(e)");
    CHECK(args[3] == R"(a\\\b)");
    CHECK(args[4] == R"(de fg)");
    CHECK(args[5] == R"(h)");
    CHECK(args[6] == R"(a\"b)");
    CHECK(args[7] == R"(c)");
    CHECK(args[8] == R"(d)");
    CHECK(args[9] == R"(a\\b c)");
    CHECK(args[10] == R"(d)");
    CHECK(args[11] == R"(e)");
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

  SUBCASE("erase_last")
  {
    Args repeated_args = Args::from_string("one two twotwo one two twotwo");

    repeated_args.erase_last("three");
    CHECK(repeated_args == Args::from_string("one two twotwo one two twotwo"));

    repeated_args.erase_last("two");
    CHECK(repeated_args == Args::from_string("one two twotwo one twotwo"));

    repeated_args.erase_last("two");
    CHECK(repeated_args == Args::from_string("one twotwo one twotwo"));

    repeated_args.erase_last("two");
    CHECK(repeated_args == Args::from_string("one twotwo one twotwo"));
  }

  SUBCASE("erase_with_prefix")
  {
    args.erase_with_prefix("m");
    CHECK(args == Args::from_string("eeny"));
  }

  SUBCASE("insert empty args")
  {
    args.insert(2, no_args);
    CHECK(args == Args::from_string("eeny meeny miny moe"));
  }

  SUBCASE("insert non-empty args")
  {
    args.insert(4, more_args);
    args.insert(2, more_args);
    args.insert(0, more_args);
    CHECK(args == Args::from_string("x y eeny meeny x y miny moe x y"));
  }

  SUBCASE("pop_back")
  {
    args.pop_back(0);
    CHECK(args == Args::from_string("eeny meeny miny moe"));

    args.pop_back();
    CHECK(args == Args::from_string("eeny meeny miny"));

    args.pop_back(2);
    CHECK(args == Args::from_string("eeny"));
  }

  SUBCASE("pop_front")
  {
    args.pop_front(0);
    CHECK(args == Args::from_string("eeny meeny miny moe"));

    args.pop_front();
    CHECK(args == Args::from_string("meeny miny moe"));

    args.pop_front(2);
    CHECK(args == Args::from_string("moe"));
  }

  SUBCASE("push_back string")
  {
    args.push_back("foo");
    CHECK(args == Args::from_string("eeny meeny miny moe foo"));
  }

  SUBCASE("push_back args")
  {
    args.push_back(more_args);
    CHECK(args == Args::from_string("eeny meeny miny moe x y"));
  }

  SUBCASE("push_front string")
  {
    args.push_front("foo");
    CHECK(args == Args::from_string("foo eeny meeny miny moe"));
  }

  SUBCASE("replace")
  {
    args.replace(3, more_args);
    args.replace(2, no_args);
    args.replace(0, more_args);
    CHECK(args == Args::from_string("x y meeny x y"));
  }
}

TEST_SUITE_END();
