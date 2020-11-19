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

TEST_CASE("Depfile::rewrite_paths")
{
  Context ctx;

  const auto cwd = ctx.actual_cwd;
  ctx.has_absolute_include_headers = true;

  const auto content = FMT("foo.o: bar.c {0}/bar.h \\\n {1}/fie.h {0}/fum.h\n",
                           cwd,
                           Util::dir_name(cwd));

  SUBCASE("Base directory not in dep file content")
  {
    ctx.config.set_base_dir("/foo/bar");
    CHECK(!Depfile::rewrite_paths(ctx, ""));
    CHECK(!Depfile::rewrite_paths(ctx, content));
  }

  SUBCASE("Base directory in dep file content but not matching")
  {
    ctx.config.set_base_dir(FMT("{}/other", Util::dir_name(cwd)));
    CHECK(!Depfile::rewrite_paths(ctx, ""));
    CHECK(!Depfile::rewrite_paths(ctx, content));
  }

  SUBCASE("Absolute paths under base directory rewritten")
  {
    ctx.config.set_base_dir(cwd);
    const auto actual = Depfile::rewrite_paths(ctx, content);
    const auto expected =
      FMT("foo.o: bar.c ./bar.h \\\n {}/fie.h ./fum.h\n", Util::dir_name(cwd));
    REQUIRE(actual);
    CHECK(*actual == expected);
  }
}

TEST_CASE("Depfile::tokenize")
{
  SUBCASE("Parse empty depfile")
  {
    std::vector<std::string> result = Depfile::tokenize("");
    CHECK(result.size() == 0);
  }

  SUBCASE("Parse simple depfile")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow meow purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Parse depfile with a dollar sign followed by a dollar sign")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$$");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$");
  }

  SUBCASE("Parse depfile with a dollar sign followed by an alphabet")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$w");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$w");
  }

  SUBCASE("Parse depfile with a backslash followed by a number sign or a colon")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\# meow\\:");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow#");
    CHECK(result[2] == "meow:");
  }

  SUBCASE("Parse depfile with a backslash followed by an alphabet")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\w purr\\r");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\w");
    CHECK(result[2] == "purr\\r");
  }

  SUBCASE("Parse depfile with a backslash followed by a space or a tab")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\ meow purr\\\tpurr");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow meow");
    CHECK(result[2] == "purr\tpurr");
  }

  SUBCASE("Parse depfile with backslashes followed by a space or a tab")
  {
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\\\\\\ meow purr\\\\ purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\ meow");
    CHECK(result[2] == "purr\\");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Parse depfile with a backslash newline")
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

  SUBCASE("Parse depfile with a new line")
  {
    // This is invalid depfile because it has multiple lines without backslash,
    // which is not valid in Makefile syntax.
    // However, Depfile::tokenize is parsing it to each token, which is
    // expected.
    std::vector<std::string> result =
      Depfile::tokenize("cat.o: meow\nmeow\npurr\n");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "purr");
  }

  SUBCASE("Parse depfile with a trailing dollar sign")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow$");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow$");
  }

  SUBCASE("Parse depfile with a trailing backslash")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow\\");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow\\");
  }

  SUBCASE("Parse depfile with a trailing backslash newline")
  {
    std::vector<std::string> result = Depfile::tokenize("cat.o: meow\\\n");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "cat.o:");
    CHECK(result[1] == "meow");
  }
}

TEST_SUITE_END();
