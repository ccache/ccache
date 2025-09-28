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

#include <ccache/context.hpp>
#include <ccache/depfile.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace fs = util::filesystem;

using TestUtil::TestContext;

TEST_SUITE_BEGIN("depfile");

TEST_CASE("depfile::escape_filename")
{
  CHECK(depfile::escape_filename("") == "");
  CHECK(depfile::escape_filename("foo") == "foo");
  CHECK(depfile::escape_filename("foo\\bar") == "foo\\\\bar");
  CHECK(depfile::escape_filename("foo#bar") == "foo\\#bar");
  CHECK(depfile::escape_filename("foo:bar") == "foo\\:bar");
  CHECK(depfile::escape_filename("foo bar") == "foo\\ bar");
  CHECK(depfile::escape_filename("foo\tbar") == "foo\\\tbar");
  CHECK(depfile::escape_filename("foo$bar") == "foo$$bar");
}

TEST_CASE("depfile::rewrite_source_paths")
{
  Context ctx;

  const fs::path cwd = ctx.actual_cwd;

  const auto content = FMT(
    "{0}/foo.o: bar.c \\\n"
    " {0}/bar/bar.h {1}/fie.h \\\n"
    "\n"
    " {0}/bar/bar.h: \n"
    " {1}/fie.h:\n",
    depfile::escape_filename(util::pstr(cwd).str()),
    depfile::escape_filename(util::pstr(cwd.parent_path()).str()));

  SUBCASE("Base directory not in dep file content")
  {
    ctx.config.set_base_dirs({"/foo/bar"});
    CHECK(!depfile::rewrite_source_paths(ctx, ""));
    CHECK(!depfile::rewrite_source_paths(ctx, content));
  }

  SUBCASE("Base directory in dep file content but not matching")
  {
    ctx.config.set_base_dirs({(cwd.parent_path() / "other").string()});
    CHECK(!depfile::rewrite_source_paths(ctx, ""));
    CHECK(!depfile::rewrite_source_paths(ctx, content));
  }

  SUBCASE("Absolute paths under base directory rewritten")
  {
    ctx.config.set_base_dirs({cwd.string()});
    const auto actual = depfile::rewrite_source_paths(ctx, content);
    const auto expected = FMT(
      "{0}/foo.o: \\\n"
      " bar.c \\\n"
      " {2} \\\n"
      " {1}/fie.h\n"
      "{2}:\n"
      "{1}/fie.h:\n",
      depfile::escape_filename(util::pstr(cwd).str()),
      depfile::escape_filename(util::pstr(cwd.parent_path()).str()),
      depfile::escape_filename(
        util::pstr(fs::path("bar/bar.h").lexically_normal()).str()));
    REQUIRE(actual);
    CHECK(*actual == expected);
  }
}

TEST_CASE("depfile::tokenize")
{
  SUBCASE("Empty")
  {
    auto result = depfile::tokenize("");
    CHECK(result.size() == 0);
  }

  SUBCASE("Simple")
  {
    auto result = depfile::tokenize("cat.o: meow meow purr");
    REQUIRE(result.size() == 6);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "meow");
    CHECK(result[4] == "purr");
    CHECK(result[5] == "");
  }

  SUBCASE("Dollar sign followed by a dollar sign")
  {
    auto result = depfile::tokenize("cat.o: meow$$");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow$");
    CHECK(result[3] == "");
  }

  SUBCASE("Dollar sign followed by an alphabet")
  {
    auto result = depfile::tokenize("cat.o: meow$w");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow$w");
    CHECK(result[3] == "");
  }

  SUBCASE("Backslash followed by a number sign or a colon")
  {
    auto result = depfile::tokenize("cat.o: meow\\# meow\\:");
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow#");
    CHECK(result[3] == "meow:");
    CHECK(result[4] == "");
  }

  SUBCASE("Backslash followed by an alphabet")
  {
    auto result = depfile::tokenize("cat.o: meow\\w purr\\r");
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow\\w");
    CHECK(result[3] == "purr\\r");
    CHECK(result[4] == "");
  }

  SUBCASE("Backslash followed by a space or a tab")
  {
    auto result = depfile::tokenize("cat.o: meow\\ meow purr\\\tpurr");
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow meow");
    CHECK(result[3] == "purr\tpurr");
    CHECK(result[4] == "");
  }

  SUBCASE("Backslashes followed by a space or a tab")
  {
    auto result = depfile::tokenize("cat.o: meow\\\\\\ meow purr\\\\ purr");
    REQUIRE(result.size() == 6);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow\\ meow");
    CHECK(result[3] == "purr\\");
    CHECK(result[4] == "purr");
    CHECK(result[5] == "");
  }

  SUBCASE("Backslash newline")
  {
    auto result = depfile::tokenize("cat.o: meow\\\nmeow\\\n purr\\\n\tpurr");
    REQUIRE(result.size() == 7);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "meow");
    CHECK(result[4] == "purr");
    CHECK(result[5] == "purr");
    CHECK(result[6] == "");
  }

  SUBCASE("Newlines")
  {
    // This is an invalid dependency file since it has multiple lines without
    // backslash, which is not valid Makefile syntax. However, the
    // depfile::tokenize's simplistic parser accepts them.
    auto result = depfile::tokenize("cat.o: meow\nmeow\npurr\n");
    REQUIRE(result.size() == 8);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
    CHECK(result[4] == "meow");
    CHECK(result[5] == "");
    CHECK(result[6] == "purr");
    CHECK(result[7] == "");
  }

  SUBCASE("Multiple entries")
  {
    auto result = depfile::tokenize(
      "foo.o bar.o: a.h \\\n"
      "  b.h\\\n"
      " c.h\n"
      "a.h:\n"
      " b.h:"); // Intentionally no newline
    REQUIRE(result.size() == 13);
    CHECK(result[0] == "foo.o");
    CHECK(result[1] == "bar.o");
    CHECK(result[2] == ":");
    CHECK(result[3] == "a.h");
    CHECK(result[4] == "b.h");
    CHECK(result[5] == "c.h");
    CHECK(result[6] == "");
    CHECK(result[7] == "a.h");
    CHECK(result[8] == ":");
    CHECK(result[9] == "");
    CHECK(result[10] == "b.h");
    CHECK(result[11] == ":");
    CHECK(result[12] == "");
  }

  SUBCASE("Trailing dollar sign")
  {
    auto result = depfile::tokenize("cat.o: meow$");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow$");
    CHECK(result[3] == "");
  }

  SUBCASE("Trailing backslash")
  {
    auto result = depfile::tokenize("cat.o: meow\\");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow\\");
    CHECK(result[3] == "");
  }

  SUBCASE("Trailing backslash newline")
  {
    auto result = depfile::tokenize("cat.o: meow\\\n");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Space before the colon but not after")
  {
    auto result = depfile::tokenize("cat.o :meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Space around the colon")
  {
    auto result = depfile::tokenize("cat.o    :    meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("No space between colon and dependency")
  {
    auto result = depfile::tokenize("cat.o:meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename (with backslashes in target)")
  {
    auto result = depfile::tokenize("e:\\cat.o: meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "e:\\cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename (with backslashes in prerequisite)")
  {
    auto result = depfile::tokenize("cat.o: c:\\meow\\purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:\\meow\\purr");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename (with slashes in target)")
  {
    auto result = depfile::tokenize("e:/cat.o: meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "e:/cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename (with slashes in prerequisite)")
  {
    auto result = depfile::tokenize("cat.o: c:/meow/purr");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat.o");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:/meow/purr");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:/meow")
  {
    auto result = depfile::tokenize("cat:/meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "/meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:\\meow")
  {
    auto result = depfile::tokenize("cat:\\meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "\\meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:\\ meow")
  {
    auto result = depfile::tokenize("cat:\\ meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == " meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:c:/meow")
  {
    auto result = depfile::tokenize("cat:c:/meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:/meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:c:\\meow")
  {
    auto result = depfile::tokenize("cat:c:\\meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:\\meow");
    CHECK(result[3] == "");
  }

  // Invalid pattern but tested for documentative purposes.
  SUBCASE("Windows filename: cat:c:")
  {
    auto result = depfile::tokenize("cat:c:");
    REQUIRE(result.size() == 5);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c");
    CHECK(result[3] == ":");
    CHECK(result[4] == "");
  }

  // Invalid pattern but tested for documentative purposes.
  SUBCASE("Windows filename: cat:c:\\")
  {
    auto result = depfile::tokenize("cat:c:\\");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:\\");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: cat:c:/")
  {
    auto result = depfile::tokenize("cat:c:/");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:/");
    CHECK(result[3] == "");
  }

  // Invalid pattern but tested for documentative purposes.
  SUBCASE("Windows filename: cat:c:meow")
  {
    auto result = depfile::tokenize("cat:c:meow");
    REQUIRE(result.size() == 6);
    CHECK(result[0] == "cat");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c");
    CHECK(result[3] == ":");
    CHECK(result[4] == "meow");
    CHECK(result[5] == "");
  }

  SUBCASE("Windows filename: c:c:/meow")
  {
    auto result = depfile::tokenize("c:c:/meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "c");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:/meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: c:c:\\meow")
  {
    auto result = depfile::tokenize("c:c:\\meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "c");
    CHECK(result[1] == ":");
    CHECK(result[2] == "c:\\meow");
    CHECK(result[3] == "");
  }

  SUBCASE("Windows filename: c:z:\\meow")
  {
    auto result = depfile::tokenize("c:z:\\meow");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "c");
    CHECK(result[1] == ":");
    CHECK(result[2] == "z:\\meow");
    CHECK(result[3] == "");
  }

  // Invalid pattern but tested for documentative purposes.
  SUBCASE("Windows filename: c:cd:\\meow")
  {
    auto result = depfile::tokenize("c:cd:\\meow");
    REQUIRE(result.size() == 6);
    CHECK(result[0] == "c");
    CHECK(result[1] == ":");
    CHECK(result[2] == "cd");
    CHECK(result[3] == ":");
    CHECK(result[4] == "\\meow");
    CHECK(result[5] == "");
  }
}

TEST_CASE("depfile::untokenize")
{
  CHECK(depfile::untokenize({}) == "");
  CHECK(depfile::untokenize({"foo.o"}) == "foo.o\n");
  CHECK(depfile::untokenize({"foo.o", ":"}) == "foo.o:\n");
  CHECK(depfile::untokenize({"foo.o", ":", "bar.h"})
        == ("foo.o: \\\n"
            " bar.h\n"));
}

TEST_SUITE_END();
