// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/environment.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>

#include <doctest/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618

namespace fs = util::filesystem;

using TestUtil::TestContext;

TEST_CASE("util::add_exe_suffix")
{
  CHECK(util::add_exe_suffix("foo") == "foo.exe");
  CHECK(util::add_exe_suffix("foo.bat") == "foo.bat");
  CHECK(util::add_exe_suffix("foo.exe") == "foo.exe");
  CHECK(util::add_exe_suffix("foo.sh") == "foo.sh");
}

TEST_CASE("util::add_extension")
{
  CHECK(util::add_extension("foo.x", "") == "foo.x");
  CHECK(util::add_extension("foo.x", ".y") == "foo.x.y");
}

TEST_CASE("util::is_full_path")
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

TEST_CASE("util::is_dev_null_path")
{
  CHECK(!util::is_dev_null_path("dev/null"));
  CHECK(util::is_dev_null_path("/dev/null"));
#ifdef _WIN32
  CHECK(util::is_dev_null_path("nul"));
  CHECK(util::is_dev_null_path("NUL"));
#endif
}

TEST_CASE("util::lexically_normal")
{
  CHECK(util::lexically_normal("") == "");
  CHECK(util::lexically_normal("/") == "/");
  CHECK(util::lexically_normal("x") == "x");
  CHECK(util::lexically_normal("x/../y") == "y");
  CHECK(util::lexically_normal("x/") == "x");
  CHECK(util::lexically_normal("x/.") == "x");
}

TEST_CASE("util::make_relative_path")
{
  using util::make_relative_path;

  const TestContext test_context;

  const std::string cwd = util::pstr(*fs::current_path());
  const std::string actual_cwd = FMT("{}/d", cwd);
#if defined(_WIN32) || defined(__CYGWIN__)
  const std::string apparent_cwd = actual_cwd;
#else
  const std::string apparent_cwd = FMT("{}/s", cwd);
#endif

  REQUIRE(fs::create_directory("d"));
#ifndef _WIN32
  REQUIRE(fs::create_symlink("d", "s"));
#endif
  REQUIRE(fs::current_path("d"));
  util::setenv("PWD", apparent_cwd);

  SUBCASE("Path matches neither actual nor apparent CWD")
  {
#ifdef _WIN32
    CHECK(make_relative_path("C:/a", "C:/b", "C:/x") == "C:/x");
#else
    CHECK(make_relative_path("/a", "/b", "/x") == "/x");
#endif
  }

  SUBCASE("Match of actual CWD")
  {
    REQUIRE(fs::create_directory("d"));

    CHECK(make_relative_path(actual_cwd, apparent_cwd, actual_cwd + "/x")
          == "x");
    CHECK(make_relative_path(actual_cwd, apparent_cwd, actual_cwd + "/d")
          == "d");
    CHECK(make_relative_path(
            actual_cwd + "/", apparent_cwd + "/", actual_cwd + "/d")
          == "d");
    CHECK(make_relative_path(
            actual_cwd + "/", apparent_cwd + "/", actual_cwd + "/d/")
          == "d");
    CHECK(make_relative_path(
            actual_cwd + "/", apparent_cwd + "/", actual_cwd + "/d/.")
          == "d");
#ifdef _WIN32
    CHECK(make_relative_path(actual_cwd, apparent_cwd, actual_cwd + "\\x")
          == "x");
    CHECK(make_relative_path(actual_cwd, apparent_cwd, actual_cwd + "\\\\x")
          == "x");
#endif
  }

#ifndef _WIN32
  SUBCASE("Match of apparent CWD")
  {
    CHECK(make_relative_path(actual_cwd, apparent_cwd, apparent_cwd + "/x")
          == "x");
  }
#endif
}

TEST_CASE("util::path_starts_with")
{
  CHECK(util::path_starts_with("", ""));
  CHECK(!util::path_starts_with("", "/"));
  CHECK(util::path_starts_with("/foo/bar", "/foo"));
  CHECK(util::path_starts_with("/foo/bar/", "/foo"));
  CHECK(util::path_starts_with("/foo/bar", "/foo/"));
  CHECK(!util::path_starts_with("/batz/bar", "/foo"));
  CHECK(!util::path_starts_with("/foo/bar", "/foo/baz"));
  CHECK(!util::path_starts_with("/beh/foo", "/foo"));
#ifdef _WIN32
  CHECK(util::path_starts_with("C:/foo/bar", "C:\\foo"));
  CHECK(util::path_starts_with("C:/foo/bar\\", "C:\\foo"));
  CHECK(util::path_starts_with("C:/foo/bar", "C:\\foo\\"));
  CHECK(util::path_starts_with("C:/foo/bar", "C:\\\\foo"));
  CHECK(util::path_starts_with("C:\\foo\\bar", "C:/foo"));
  CHECK(util::path_starts_with("C:\\\\foo\\\\bar", "C:/foo"));
  CHECK(util::path_starts_with("C:/FOO/BAR", "c:\\foo"));
  CHECK(util::path_starts_with("c:/foo/bar", "C:\\FOO"));
  CHECK(util::path_starts_with("c:/foo/bar/", "C:\\FOO"));
  CHECK(util::path_starts_with("c:/foo/bar", "C:\\FOO\\"));
  CHECK(!util::path_starts_with("C:\\foo\\bar", "/foo/baz"));
  CHECK(!util::path_starts_with("C:\\foo\\bar", "C:/foo/baz"));
  CHECK(!util::path_starts_with("C:\\beh\\foo", "/foo"));
  CHECK(!util::path_starts_with("C:\\beh\\foo", "C:/foo"));
#endif
}

TEST_CASE("util::with_extension")
{
  CHECK(util::with_extension("foo.x", "") == "foo");
  CHECK(util::with_extension("foo.x", ".y") == "foo.y");
}
