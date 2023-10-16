// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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
#include <util/fmtmacros.hpp>
#include <util/path.hpp>

#include <third_party/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618

TEST_CASE("util::add_exe_suffix")
{
  CHECK(util::add_exe_suffix("foo") == "foo.exe");
  CHECK(util::add_exe_suffix("foo.bat") == "foo.bat");
  CHECK(util::add_exe_suffix("foo.exe") == "foo.exe");
  CHECK(util::add_exe_suffix("foo.sh") == "foo.sh");
}

TEST_CASE("util::is_absolute_path")
{
#ifdef _WIN32
  CHECK(util::is_absolute_path("C:/"));
  CHECK(util::is_absolute_path("C:\\"));
  CHECK(util::is_absolute_path("C:\\foo/fie"));
  CHECK(util::is_absolute_path("/C:\\foo/fie")); // MSYS/Cygwin path
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("C:"));
  CHECK(!util::is_absolute_path("foo\\fie/fum"));
  CHECK(!util::is_absolute_path("C:foo/fie"));
#endif
  CHECK(util::is_absolute_path("/"));
  CHECK(util::is_absolute_path("/foo/fie"));
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("foo/fie"));
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

TEST_CASE("util::to_absolute_path")
{
  CHECK(util::to_absolute_path("/foo/bar") == "/foo/bar");

#ifdef _WIN32
  CHECK(util::to_absolute_path("C:\\foo\\bar") == "C:\\foo\\bar");
#endif

  const auto cwd = util::actual_cwd();

  CHECK(util::to_absolute_path("") == cwd);
  CHECK(util::to_absolute_path(".") == cwd);
  CHECK(util::to_absolute_path("..") == Util::dir_name(cwd));
  CHECK(util::to_absolute_path("foo") == FMT("{}/foo", cwd));
  CHECK(util::to_absolute_path("../foo/bar")
        == FMT("{}/foo/bar", Util::dir_name(cwd)));
}

TEST_CASE("util::to_absolute_path_no_drive")
{
  CHECK(util::to_absolute_path_no_drive("/foo/bar") == "/foo/bar");

#ifdef _WIN32
  CHECK(util::to_absolute_path_no_drive("C:\\foo\\bar") == "\\foo\\bar");
#endif

  auto cwd = util::actual_cwd();
#ifdef _WIN32
  cwd = cwd.substr(2);
#endif

  CHECK(util::to_absolute_path_no_drive("") == cwd);
  CHECK(util::to_absolute_path_no_drive(".") == cwd);
  CHECK(util::to_absolute_path_no_drive("..") == Util::dir_name(cwd));
  CHECK(util::to_absolute_path_no_drive("foo") == FMT("{}/foo", cwd));
  CHECK(util::to_absolute_path_no_drive("../foo/bar")
        == FMT("{}/foo/bar", Util::dir_name(cwd)));
}

TEST_CASE("util::path_starts_with")
{
  CHECK(!util::path_starts_with("", ""));
  CHECK(!util::path_starts_with("", "/"));
  CHECK(util::path_starts_with("/foo/bar", "/foo"));
  CHECK(!util::path_starts_with("/batz/bar", "/foo"));
  CHECK(!util::path_starts_with("/foo/bar", "/foo/baz"));
  CHECK(!util::path_starts_with("/beh/foo", "/foo"));
#ifdef _WIN32
  CHECK(util::path_starts_with("C:/foo/bar", "C:\\foo"));
  CHECK(util::path_starts_with("C:/foo/bar", "C:\\\\foo"));
  CHECK(util::path_starts_with("C:\\foo\\bar", "C:/foo"));
  CHECK(util::path_starts_with("C:\\\\foo\\\\bar", "C:/foo"));
  CHECK(util::path_starts_with("C:/FOO/BAR", "c:\\foo"));
  CHECK(util::path_starts_with("c:/foo/bar", "C:\\FOO"));
  CHECK(!util::path_starts_with("C:\\foo\\bar", "/foo/baz"));
  CHECK(!util::path_starts_with("C:\\foo\\bar", "C:/foo/baz"));
  CHECK(!util::path_starts_with("C:\\beh\\foo", "/foo"));
  CHECK(!util::path_starts_with("C:\\beh\\foo", "C:/foo"));
#endif
}
