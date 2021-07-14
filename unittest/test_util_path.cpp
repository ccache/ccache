// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <Util.hpp>
#include <fmtmacros.hpp>
#include <util/path.hpp>

#include <third_party/doctest.h>

TEST_CASE("util::is_absolute_path")
{
#ifdef _WIN32
  CHECK(util::is_absolute_path("C:/"));
  CHECK(util::is_absolute_path("C:\\foo/fie"));
  CHECK(util::is_absolute_path("/C:\\foo/fie")); // MSYS/Cygwin path
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("foo\\fie/fum"));
  CHECK(!util::is_absolute_path("C:foo/fie"));
#endif
  CHECK(util::is_absolute_path("/"));
  CHECK(util::is_absolute_path("/foo/fie"));
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("foo/fie"));
}

TEST_CASE("util::is_absolute_path")
{
  CHECK(!util::is_full_path(""));
  CHECK(!util::is_full_path("foo"));
  CHECK(util::is_full_path("/foo"));
  CHECK(util::is_full_path("foo/"));
  CHECK(util::is_full_path("foo/bar"));
#ifdef _WIN32
  CHECK(util::is_full_path("foo\\bar"));
#else
  CHECK(!util::is_full_path("foo\\bar"));
#endif
}

TEST_CASE("util::split_path_list")
{
  CHECK(util::split_path_list("").empty());
  {
    const auto v = util::split_path_list("a");
    REQUIRE(v.size() == 1);
    CHECK(v[0] == "a");
  }
  {
    const auto v = util::split_path_list("a/b");
    REQUIRE(v.size() == 1);
    CHECK(v[0] == "a/b");
  }
  {
#ifdef _WIN32
    const auto v = util::split_path_list("a/b;c");
#else
    const auto v = util::split_path_list("a/b:c");
#endif
    REQUIRE(v.size() == 2);
    CHECK(v[0] == "a/b");
    CHECK(v[1] == "c");
  }
}

TEST_CASE("util::to_absolute_path")
{
  CHECK(util::to_absolute_path("/foo/bar") == "/foo/bar");

#ifdef _WIN32
  CHECK(util::to_absolute_path("C:\\foo\\bar") == "C:\\foo\\bar");
#endif

  const auto cwd = Util::get_actual_cwd();

  CHECK(util::to_absolute_path("") == cwd);
  CHECK(util::to_absolute_path(".") == cwd);
  CHECK(util::to_absolute_path("..") == Util::dir_name(cwd));
  CHECK(util::to_absolute_path("foo") == FMT("{}/foo", cwd));
  CHECK(util::to_absolute_path("../foo/bar")
        == FMT("{}/foo/bar", Util::dir_name(cwd)));
}
