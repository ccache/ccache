// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include "../src/Config.hpp"
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include <core/exceptions.hpp>
#include <util/environment.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/path.hpp>
#include <util/wincompat.hpp>

#include "third_party/doctest.h"

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

TEST_CASE("Util::base_name")
{
  CHECK(Util::base_name("") == "");
  CHECK(Util::base_name(".") == ".");
  CHECK(Util::base_name("foo") == "foo");
  CHECK(Util::base_name("/") == "");
  CHECK(Util::base_name("/foo") == "foo");
  CHECK(Util::base_name("/foo/bar/f.txt") == "f.txt");
}

TEST_CASE("Util::change_extension")
{
  CHECK(Util::change_extension("", "") == "");
  CHECK(Util::change_extension("x", "") == "x");
  CHECK(Util::change_extension("", "x") == "x");
  CHECK(Util::change_extension("", ".") == ".");
  CHECK(Util::change_extension(".", "") == "");
  CHECK(Util::change_extension("...", "x") == "..x");
  CHECK(Util::change_extension("abc", "def") == "abcdef");
  CHECK(Util::change_extension("dot.", ".dot") == "dot.dot");
  CHECK(Util::change_extension("foo.ext", "e2") == "fooe2");
  CHECK(Util::change_extension("bar.txt", ".o") == "bar.o");
  CHECK(Util::change_extension("foo.bar.txt", ".o") == "foo.bar.o");
}

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

TEST_CASE("Util::dir_name")
{
  CHECK(Util::dir_name("") == ".");
  CHECK(Util::dir_name(".") == ".");
  CHECK(Util::dir_name("foo") == ".");
  CHECK(Util::dir_name("/") == "/");
  CHECK(Util::dir_name("/foo") == "/");
  CHECK(Util::dir_name("/foo/bar/f.txt") == "/foo/bar");

#ifdef _WIN32
  CHECK(Util::dir_name("C:/x/y") == "C:/x");
  CHECK(Util::dir_name("X:/x/y") == "X:/x");
  CHECK(Util::dir_name("C:\\x\\y") == "C:\\x");
  CHECK(Util::dir_name("C:/x") == "C:/");
  CHECK(Util::dir_name("C:\\x") == "C:\\");
  CHECK(Util::dir_name("C:/") == "C:/");
  CHECK(Util::dir_name("C:\\") == "C:\\");
#endif
}

TEST_CASE("Util::get_extension")
{
  CHECK(Util::get_extension("") == "");
  CHECK(Util::get_extension(".") == ".");
  CHECK(Util::get_extension("...") == ".");
  CHECK(Util::get_extension("foo") == "");
  CHECK(Util::get_extension("/") == "");
  CHECK(Util::get_extension("/foo") == "");
  CHECK(Util::get_extension("/foo/bar/f") == "");
  CHECK(Util::get_extension("f.txt") == ".txt");
  CHECK(Util::get_extension("f.abc.txt") == ".txt");
  CHECK(Util::get_extension("/foo/bar/f.txt") == ".txt");
  CHECK(Util::get_extension("/foo/bar/f.abc.txt") == ".txt");
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

TEST_CASE("Util::is_absolute_path_with_prefix")
{
  CHECK(*Util::is_absolute_path_with_prefix("-I/c/foo") == 2);
  CHECK(*Util::is_absolute_path_with_prefix("-W,path/c/foo") == 7);
  CHECK(!Util::is_absolute_path_with_prefix("-DMACRO"));
#ifdef _WIN32
  CHECK(*Util::is_absolute_path_with_prefix("-I/C:/foo") == 2);
  CHECK(*Util::is_absolute_path_with_prefix("-IC:/foo") == 2);
  CHECK(*Util::is_absolute_path_with_prefix("-W,path/c:/foo") == 7);
  CHECK(*Util::is_absolute_path_with_prefix("-W,pathc:/foo") == 7);
  CHECK(!Util::is_absolute_path_with_prefix("-opt:value"));
#endif
}

TEST_CASE("Util::is_dir_separator")
{
  CHECK(!Util::is_dir_separator('x'));
  CHECK(Util::is_dir_separator('/'));
#ifdef _WIN32
  CHECK(Util::is_dir_separator('\\'));
#else
  CHECK(!Util::is_dir_separator('\\'));
#endif
}

TEST_CASE("Util::make_relative_path")
{
  using Util::make_relative_path;

  const TestContext test_context;

  const std::string cwd = util::actual_cwd();
  const std::string actual_cwd = FMT("{}/d", cwd);
#ifdef _WIN32
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
      == ".\\x");
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
#ifndef _WIN32
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

TEST_CASE("Util::remove_extension")
{
  CHECK(Util::remove_extension("") == "");
  CHECK(Util::remove_extension(".") == "");
  CHECK(Util::remove_extension("...") == "..");
  CHECK(Util::remove_extension("foo") == "foo");
  CHECK(Util::remove_extension("/") == "/");
  CHECK(Util::remove_extension("/foo") == "/foo");
  CHECK(Util::remove_extension("/foo/bar/f") == "/foo/bar/f");
  CHECK(Util::remove_extension("f.txt") == "f");
  CHECK(Util::remove_extension("f.abc.txt") == "f.abc");
  CHECK(Util::remove_extension("/foo/bar/f.txt") == "/foo/bar/f");
  CHECK(Util::remove_extension("/foo/bar/f.abc.txt") == "/foo/bar/f.abc");
}

TEST_SUITE_END();
