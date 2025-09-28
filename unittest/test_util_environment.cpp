// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/environment.hpp>
#include <ccache/util/filesystem.hpp>

#include <doctest/doctest.h>

#include <vector>

namespace fs = util::filesystem;

TEST_SUITE_BEGIN("util");

#ifdef _WIN32

TEST_CASE("util::getenv_path_list")
{
  SUBCASE("unset")
  {
    util::unsetenv("test");
    std::vector<fs::path> expected;
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("empty")
  {
    util::setenv("test", "");
    std::vector<fs::path> expected = {};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("delimiters")
  {
    util::setenv("test", ";;");
    std::vector<fs::path> expected = {};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("multiple")
  {
    util::setenv("test", "c:\\foo;/bar");
    std::vector<fs::path> expected = {"c:\\foo", "/bar"};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("delimiters around")
  {
    util::setenv("test", ";c:\\foo;");
    std::vector<fs::path> expected = {"c:\\foo"};
    CHECK(util::getenv_path_list("test") == expected);
  }
}

#else

TEST_CASE("util::getenv_path_list")
{
  SUBCASE("unset")
  {
    util::unsetenv("test");
    std::vector<fs::path> expected;
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("empty")
  {
    util::setenv("test", "");
    std::vector<fs::path> expected = {};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("delimiters")
  {
    util::setenv("test", "::");
    std::vector<fs::path> expected = {};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("multiple")
  {
    util::setenv("test", "/foo:/bar");
    std::vector<fs::path> expected = {"/foo", "/bar"};
    CHECK(util::getenv_path_list("test") == expected);
  }

  SUBCASE("delimiters around")
  {
    util::setenv("test", ":/foo:");
    std::vector<fs::path> expected = {"/foo"};
    CHECK(util::getenv_path_list("test") == expected);
  }
}

#endif

TEST_CASE("util::expand_environment_variables")
{
  util::setenv("FOO", "bar");

  CHECK(util::expand_environment_variables("") == "");
  CHECK(util::expand_environment_variables("$FOO") == "bar");
  CHECK(util::expand_environment_variables("$$FOO") == "$FOO");
  CHECK(util::expand_environment_variables("$$$FOO") == "$bar");
  CHECK(util::expand_environment_variables("$ $$ $") == "$ $ $");
  CHECK(util::expand_environment_variables("$FOO $FOO:$FOO") == "bar bar:bar");
  CHECK(util::expand_environment_variables("x$FOO") == "xbar");
  CHECK(util::expand_environment_variables("${FOO}x") == "barx");

  CHECK(util::expand_environment_variables("$surelydoesntexist").error()
        == "environment variable \"surelydoesntexist\" not set");
  CHECK(util::expand_environment_variables("${FOO").error()
        == "syntax error: missing '}' after \"FOO\"");
}

TEST_SUITE_END();
