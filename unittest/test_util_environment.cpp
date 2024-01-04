// Copyright (C) 2023 Joel Rosdahl and other contributors
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

#include <util/environment.hpp>

#include <third_party/doctest.h>

TEST_SUITE_BEGIN("util");

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
