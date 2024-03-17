// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include "TestUtil.hpp"

#include <ccache/Config.hpp>
#include <ccache/Util.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest.h>
#include <fcntl.h>

#include <optional>
#include <string>
#include <vector>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>

using TestUtil::TestContext;

namespace fs = util::filesystem;

TEST_SUITE_BEGIN("Util");

TEST_CASE("Util::common_dir_prefix_length")
{
  CHECK(Util::common_dir_prefix_length("", "") == 0);
  CHECK(Util::common_dir_prefix_length("/", "") == 0);
  CHECK(Util::common_dir_prefix_length("", "/") == 0);
  CHECK(Util::common_dir_prefix_length("/", "/") == 0);
  CHECK(Util::common_dir_prefix_length("/", "/b") == 0);
  CHECK(Util::common_dir_prefix_length("/a", "/") == 0);
  CHECK(Util::common_dir_prefix_length("/a", "/b") == 0);
  CHECK(Util::common_dir_prefix_length("/a", "/a") == 2);
  CHECK(Util::common_dir_prefix_length("/a", "/a/b") == 2);
  CHECK(Util::common_dir_prefix_length("/a/b", "/a") == 2);
  CHECK(Util::common_dir_prefix_length("/a/b", "/a/c") == 2);
  CHECK(Util::common_dir_prefix_length("/a/b", "/a/b") == 4);
  CHECK(Util::common_dir_prefix_length("/a/bc", "/a/b") == 2);
  CHECK(Util::common_dir_prefix_length("/a/b", "/a/bc") == 2);
}

TEST_CASE("Util::get_relative_path")
{
#ifdef _WIN32
  CHECK(Util::get_relative_path("C:/a", "C:/a") == ".");
  CHECK(Util::get_relative_path("C:/a", "Z:/a") == "Z:/a");
  CHECK(Util::get_relative_path("C:/a/b", "C:/a") == "..");
  CHECK(Util::get_relative_path("C:/a", "C:/a/b") == "b");
  CHECK(Util::get_relative_path("C:/a", "C:/a/b/c") == "b/c");
  CHECK(Util::get_relative_path("C:/a/b", "C:/a/c") == "../c");
  CHECK(Util::get_relative_path("C:/a/b", "C:/a/c/d") == "../c/d");
  CHECK(Util::get_relative_path("C:/a/b/c", "C:/a/c/d") == "../../c/d");
  CHECK(Util::get_relative_path("C:/a/b", "C:/") == "../..");
  CHECK(Util::get_relative_path("C:/a/b", "C:/c") == "../../c");
  CHECK(Util::get_relative_path("C:/", "C:/a/b") == "a/b");
  CHECK(Util::get_relative_path("C:/a", "D:/a/b") == "D:/a/b");
#else
  CHECK(Util::get_relative_path("/a", "/a") == ".");
  CHECK(Util::get_relative_path("/a/b", "/a") == "..");
  CHECK(Util::get_relative_path("/a", "/a/b") == "b");
  CHECK(Util::get_relative_path("/a", "/a/b/c") == "b/c");
  CHECK(Util::get_relative_path("/a/b", "/a/c") == "../c");
  CHECK(Util::get_relative_path("/a/b", "/a/c/d") == "../c/d");
  CHECK(Util::get_relative_path("/a/b/c", "/a/c/d") == "../../c/d");
  CHECK(Util::get_relative_path("/a/b", "/") == "../..");
  CHECK(Util::get_relative_path("/a/b", "/c") == "../../c");
  CHECK(Util::get_relative_path("/", "/a/b") == "a/b");
#endif
}

TEST_CASE("Util::make_relative_path")
{
  using Util::make_relative_path;

  const TestContext test_context;

  const std::string cwd = util::actual_cwd();
  const std::string actual_cwd = FMT("{}/d", cwd);
#if defined(_WIN32) || defined(__CYGWIN__)
  const std::string apparent_cwd = actual_cwd;
#else
  const std::string apparent_cwd = FMT("{}/s", cwd);
#endif

  REQUIRE(fs::create_directory("d"));
#ifndef _WIN32
  REQUIRE(symlink("d", "s") == 0);
#endif
  REQUIRE(fs::current_path("d"));
  util::setenv("PWD", apparent_cwd);
  SUBCASE("No base directory")
  {
    CHECK(make_relative_path("", "/a", "/a", "/a/x") == "/a/x");
  }

  SUBCASE("Path matches neither actual nor apparent CWD")
  {
#ifdef _WIN32
    CHECK(make_relative_path("C:/", "C:/a", "C:/b", "C:/x") == "C:/x");
#else
    CHECK(make_relative_path("/", "/a", "/b", "/x") == "/x");
#endif
  }

  SUBCASE("Match of actual CWD")
  {
#ifdef _WIN32
    CHECK(
      make_relative_path(
        actual_cwd.substr(0, 3), actual_cwd, apparent_cwd, actual_cwd + "/x")
      == "./x");
    CHECK(
      make_relative_path(
        actual_cwd.substr(0, 3), actual_cwd, apparent_cwd, actual_cwd + "\\x")
      == ".\\x");
    CHECK(
      make_relative_path(
        actual_cwd.substr(0, 3), actual_cwd, apparent_cwd, actual_cwd + "\\\\x")
      == ".\\\\x");
#else
    CHECK(make_relative_path("/", actual_cwd, apparent_cwd, actual_cwd + "/x")
          == "./x");
#endif
  }

#ifndef _WIN32
  SUBCASE("Match of apparent CWD")
  {
    CHECK(make_relative_path("/", actual_cwd, apparent_cwd, apparent_cwd + "/x")
          == "./x");
  }

  SUBCASE("Match if using resolved (using realpath(3)) path")
  {
    CHECK(make_relative_path("/", actual_cwd, actual_cwd, apparent_cwd + "/x")
          == "./x");
  }
#endif
}

TEST_CASE("Util::normalize_abstract_absolute_path")
{
  CHECK(Util::normalize_abstract_absolute_path("") == "");
  CHECK(Util::normalize_abstract_absolute_path(".") == ".");
  CHECK(Util::normalize_abstract_absolute_path("..") == "..");
  CHECK(Util::normalize_abstract_absolute_path("...") == "...");
  CHECK(Util::normalize_abstract_absolute_path("x/./") == "x/./");

#ifdef _WIN32
  CHECK(Util::normalize_abstract_absolute_path("c:/") == "c:/");
  CHECK(Util::normalize_abstract_absolute_path("c:\\") == "c:/");
  CHECK(Util::normalize_abstract_absolute_path("c:/.") == "c:/");
  CHECK(Util::normalize_abstract_absolute_path("c:\\..") == "c:/");
  CHECK(Util::normalize_abstract_absolute_path("c:\\x/..") == "c:/");
  CHECK(Util::normalize_abstract_absolute_path("c:\\x/./y\\..\\\\z")
        == "c:/x/z");
#else
  CHECK(Util::normalize_abstract_absolute_path("/") == "/");
  CHECK(Util::normalize_abstract_absolute_path("/.") == "/");
  CHECK(Util::normalize_abstract_absolute_path("/..") == "/");
  CHECK(Util::normalize_abstract_absolute_path("/./") == "/");
  CHECK(Util::normalize_abstract_absolute_path("//") == "/");
  CHECK(Util::normalize_abstract_absolute_path("/../x") == "/x");
  CHECK(Util::normalize_abstract_absolute_path("/x/./y/z") == "/x/y/z");
  CHECK(Util::normalize_abstract_absolute_path("/x/../y/z/") == "/y/z");
  CHECK(Util::normalize_abstract_absolute_path("/x/.../y/z") == "/x/.../y/z");
  CHECK(Util::normalize_abstract_absolute_path("/x/yyy/../zz") == "/x/zz");
  CHECK(Util::normalize_abstract_absolute_path("//x/yyy///.././zz") == "/x/zz");
#endif
}

TEST_CASE("Util::normalize_concrete_absolute_path")
{
#if !defined(_WIN32) && !defined(__CYGWIN__)
  TestContext test_context;

  util::write_file("file", "");
  REQUIRE(fs::create_directories("dir1/dir2"));
  REQUIRE(symlink("dir1/dir2", "symlink") == 0);
  const auto cwd = util::actual_cwd();

  CHECK(Util::normalize_concrete_absolute_path(FMT("{}/file", cwd))
        == FMT("{}/file", cwd));
  CHECK(Util::normalize_concrete_absolute_path(FMT("{}/dir1/../file", cwd))
        == FMT("{}/file", cwd));
  CHECK(Util::normalize_concrete_absolute_path(FMT("{}/symlink/../file", cwd))
        == FMT("{}/symlink/../file", cwd));
#endif
}

TEST_SUITE_END();
