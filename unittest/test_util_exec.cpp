// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/util/exec.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

using TestUtil::TestContext;

TEST_CASE("util::exec_to_string")
{
  using util::exec_to_string;

  TestContext test_context;

  SUBCASE("stdout + stderr")
  {
#ifdef _WIN32
    REQUIRE(util::write_file("command.bat",
                             "@echo off\r\necho fisk\r\necho sork>&2"));
    util::Args args{"command.bat"};
#else
    util::Args args{"sh", "-c", "echo fisk; echo sork >&2"};
#endif
    auto result = exec_to_string(args);
    REQUIRE(result);
#ifdef _WIN32
    CHECK(*result == "fisk\r\nsork\r\n");
#else
    CHECK(*result == "fisk\nsork\n");
#endif
  }

  SUBCASE("error")
  {
    auto result = exec_to_string({"doesnotexist"});
    REQUIRE(!result);
#ifdef _WIN32
    CHECK(util::starts_with(result.error(), "CreateProcess failure: "));
#else
    CHECK(result.error() == "posix_spawnp failed: No such file or directory");
#endif
  }
}
