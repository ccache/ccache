// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "../src/Context.hpp"
#include "../src/Depfile.hpp"
#include "../src/fmtmacros.hpp"
#include "TestUtil.hpp"

#include "third_party/doctest.h"

using TestUtil::TestContext;

TEST_SUITE_BEGIN("Depfile");

TEST_CASE("Depfile::escape_filename")
{
  CHECK(Depfile::escape_filename("") == "");
  CHECK(Depfile::escape_filename("foo") == "foo");
  CHECK(Depfile::escape_filename("foo\\bar") == "foo\\\\bar");
  CHECK(Depfile::escape_filename("foo#bar") == "foo\\#bar");
  CHECK(Depfile::escape_filename("foo bar") == "foo\\ bar");
  CHECK(Depfile::escape_filename("foo\tbar") == "foo\\\tbar");
  CHECK(Depfile::escape_filename("foo$bar") == "foo$$bar");
}

TEST_CASE("Depfile::rewrite_source_paths")
{
  Context ctx;

  const auto cwd = ctx.actual_cwd;

  const auto content =
    FMT("{0}/foo.o {0}/foo.o: bar.c {0}/bar.h \\\n\n {1}/fie.h {0}/fum.h\n",
        cwd,
        Util::dir_name(cwd));

  SUBCASE("Base directory not in dep file content")
  {
    ctx.config.set_base_dir("/foo/bar");
    CHECK(!Depfile::rewrite_source_paths(ctx, ""));
    CHECK(!Depfile::rewrite_source_paths(ctx, content));
  }

  SUBCASE("Base directory in dep file content but not matching")
  {
    ctx.config.set_base_dir(FMT("{}/other", Util::dir_name(cwd)));
    CHECK(!Depfile::rewrite_source_paths(ctx, ""));
    CHECK(!Depfile::rewrite_source_paths(ctx, content));
  }

  SUBCASE("Absolute paths under base directory rewritten")
  {
    ctx.config.set_base_dir(cwd);
    const auto actual = Depfile::rewrite_source_paths(ctx, content);
    const auto expected =
      FMT("{0}/foo.o {0}/foo.o: bar.c ./bar.h \\\n\n {1}/fie.h ./fum.h\n",
          cwd,
          Util::dir_name(cwd));
    REQUIRE(actual);
    CHECK(*actual == expected);
  }
}

TEST_CASE("Depfile::tokenize")
{
  SUBCASE("Empty")
  {
    std::vector<std::string> result = Depfile::tokenize("");
    CHECK(result.size() == 0);
  }

  SUBCASE("Simple")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow meow purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Dollar sign followed by a dollar sign")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$$");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$");
  }

  SUBCASE("Dollar sign followed by an alphabet")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$w");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$w");
  }

  SUBCASE("Backslash followed by a number sign or a colon")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\# meow\\:");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow#");
    CHECK(result[2] == "meow:");
  }

  SUBCASE("Backslash followed by an alphabet")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\w purr\\r");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\w");
    CHECK(result[2] == "purr\\r");
  }

  SUBCASE("Backslash followed by a space or a tab")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\ meow purr\\\tpurr");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow meow");
    CHECK(result[2] == "purr\tpurr");
  }

  SUBCASE("Backslashes followed by a space or a tab")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\\\\\ meow purr\\\\ purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\ meow");
    CHECK(result[2] == "purr\\");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Backslash newline")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\\nmeow\\\n purr\\\n\tpurr");
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "purr");
    CHECK(result[4] == "purr");
  }

  SUBCASE("Newlines")
  {
    // This is an invalid dependency file since it has multiple lines without
    // backslash, which is not valid Makefile syntax. However, the
    // Depfile::tokenize's simplistic parser accepts them.
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\nmeow\npurr\n");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Trailing dollar sign")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$");
  }

  SUBCASE("Trailing backslash")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow\\");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\");
  }

  SUBCASE("Trailing backslash newline")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow\\\n");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("Space before the colon but not after")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o :meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("Space around the colon")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o    :    meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("No space between colon and dependency")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o:meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("Windows filename (with backslashes in target)")
  {
    std::vector<std::string> result = Depfile::tokenize("e:\\cat.o: meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "e:\\cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("Windows filename (with backslashes in prerequisite)")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: c:\\meow\\purr");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "c:\\meow\\purr");
  }

  SUBCASE("Windows filename (with slashes in target)")
  {
    std::vector<std::string> result = Depfile::tokenize("e:/cat.o: meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "e:/cat.o:");
    CHECK(result[1] == "meow");
  }

  SUBCASE("Windows filename (with slashes in prerequisite)")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: c:/meow/purr");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "c:/meow/purr");
  }

  SUBCASE("Windows filename (with slashes and trailing colon)")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: c: /meow/purr");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "c:");
    CHECK(result[2] == "/meow/purr");
  }

  SUBCASE("Windows filename: cat:/meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:/meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "/meow");
  }

  SUBCASE("Windows filename: cat:\\meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:\\meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "\\meow");
  }

  SUBCASE("Windows filename: cat:\\ meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:\\ meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == " meow");
  }

  SUBCASE("Windows filename: cat:c:/meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:/meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:/meow");
  }

  SUBCASE("Windows filename: cat:c:\\meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:\\meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:\\meow");
  }

  SUBCASE("Windows filename: cat:c:")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:");
  }

  SUBCASE("Windows filename: cat:c:\\")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:\\");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:\\");
  }

  SUBCASE("Windows filename: cat:c:/")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:/");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:/");
  }

  SUBCASE("Windows filename: cat:c:meow")
  {
    std::vector<std::string> result = Depfile::tokenize("cat:c:meow");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat:");
    CHECK(result[1] == "c:");
    CHECK(result[2] == "meow");
  }

  SUBCASE("Windows filename: c:c:/meow")
  {
    std::vector<std::string> result = Depfile::tokenize("c:c:/meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "c:");
    CHECK(result[1] == "c:/meow");
  }

  SUBCASE("Windows filename: c:c:\\meow")
  {
    std::vector<std::string> result = Depfile::tokenize("c:c:\\meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "c:");
    CHECK(result[1] == "c:\\meow");
  }

  SUBCASE("Windows filename: c:z:\\meow")
  {
    std::vector<std::string> result = Depfile::tokenize("c:z:\\meow");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "c:");
    CHECK(result[1] == "z:\\meow");
  }

  SUBCASE("Windows filename: c:cd:\\meow")
  {
    std::vector<std::string> result = Depfile::tokenize("c:cd:\\meow");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "c:");
    CHECK(result[1] == "cd:");
    CHECK(result[2] == "\\meow");
  }
}

TEST_SUITE_END();
