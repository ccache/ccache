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
#include "../src/Fd.hpp"
#include "../src/Util.hpp"
#include "../src/fmtmacros.hpp"
#include "TestUtil.hpp"

#include <Stat.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <util/environment.hpp>
#include <util/file.hpp>

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

TEST_CASE("Util::create_dir")
{
  TestContext test_context;

  CHECK(Util::create_dir("/"));

  CHECK(Util::create_dir("create/dir"));
  CHECK(Stat::stat("create/dir").is_directory());

  util::write_file("create/dir/file", "");
  CHECK(!Util::create_dir("create/dir/file"));
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

TEST_CASE("Util::strip_ansi_csi_seqs")
{
  const char input[] =
    "Normal,"
    " \x1B[K\x1B[1mbold\x1B[m,"
    " \x1B[31mred\x1B[m,"
    " \x1B[1;32mbold green\x1B[m.\n";

  CHECK(Util::strip_ansi_csi_seqs(input) == "Normal, bold, red, bold green.\n");
}

TEST_CASE("Util::ensure_dir_exists")
{
  TestContext test_context;

  CHECK_NOTHROW(Util::ensure_dir_exists("/"));

  CHECK_NOTHROW(Util::ensure_dir_exists("create/dir"));
  CHECK(Stat::stat("create/dir").is_directory());

  util::write_file("create/dir/file", "");
  CHECK_THROWS_WITH(
    Util::ensure_dir_exists("create/dir/file"),
    "Failed to create directory create/dir/file: Not a directory");
}

TEST_CASE("Util::format_argv_for_logging")
{
  const char* argv_0[] = {nullptr};
  CHECK(Util::format_argv_for_logging(argv_0) == "");

  const char* argv_2[] = {"foo", "bar", nullptr};
  CHECK(Util::format_argv_for_logging(argv_2) == "foo bar");
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

static inline std::string
os_path(std::string path)
{
#if defined(_WIN32) && !defined(HAVE_DIRENT_H)
  std::replace(path.begin(), path.end(), '/', '\\');
#endif

  return path;
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

TEST_CASE("Util::is_ccache_executable")
{
  CHECK(Util::is_ccache_executable("ccache"));
  CHECK(Util::is_ccache_executable("ccache-1.2.3"));
  CHECK(!Util::is_ccache_executable("fooccache"));
  CHECK(!Util::is_ccache_executable("gcc"));
#ifdef _WIN32
  CHECK(Util::is_ccache_executable("CCACHE"));
  CHECK(Util::is_ccache_executable("CCACHE.exe"));
  CHECK(Util::is_ccache_executable("CCACHE-1.2.3"));
  CHECK(Util::is_ccache_executable("CCACHE.EXE"));
  CHECK(Util::is_ccache_executable("CCACHE-1.2.3.EXE"));
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

  const std::string cwd = Util::get_actual_cwd();
  const std::string actual_cwd = FMT("{}/d", cwd);
#ifdef _WIN32
  const std::string apparent_cwd = actual_cwd;
#else
  const std::string apparent_cwd = FMT("{}/s", cwd);
#endif

  REQUIRE(Util::create_dir("d"));
#ifndef _WIN32
  REQUIRE(symlink("d", "s") == 0);
#endif
  REQUIRE(chdir("d") == 0);
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

TEST_CASE("Util::matches_dir_prefix_or_file")
{
  CHECK(!Util::matches_dir_prefix_or_file("", ""));
  CHECK(!Util::matches_dir_prefix_or_file("/", ""));
  CHECK(!Util::matches_dir_prefix_or_file("", "/"));

  CHECK(Util::matches_dir_prefix_or_file("aa", "aa"));
  CHECK(!Util::matches_dir_prefix_or_file("aaa", "aa"));
  CHECK(!Util::matches_dir_prefix_or_file("aa", "aaa"));
  CHECK(!Util::matches_dir_prefix_or_file("aa/", "aa"));

  CHECK(Util::matches_dir_prefix_or_file("/aa/bb", "/aa/bb"));
  CHECK(!Util::matches_dir_prefix_or_file("/aa/b", "/aa/bb"));
  CHECK(!Util::matches_dir_prefix_or_file("/aa/bbb", "/aa/bb"));

  CHECK(Util::matches_dir_prefix_or_file("/aa", "/aa/bb"));
  CHECK(Util::matches_dir_prefix_or_file("/aa/", "/aa/bb"));
  CHECK(!Util::matches_dir_prefix_or_file("/aa/bb", "/aa"));
  CHECK(!Util::matches_dir_prefix_or_file("/aa/bb", "/aa/"));

#ifdef _WIN32
  CHECK(Util::matches_dir_prefix_or_file("\\aa", "\\aa\\bb"));
  CHECK(Util::matches_dir_prefix_or_file("\\aa\\", "\\aa\\bb"));
#else
  CHECK(!Util::matches_dir_prefix_or_file("\\aa", "\\aa\\bb"));
  CHECK(!Util::matches_dir_prefix_or_file("\\aa\\", "\\aa\\bb"));
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
  REQUIRE(Util::create_dir("dir1/dir2"));
  REQUIRE(symlink("dir1/dir2", "symlink") == 0);
  const auto cwd = Util::get_actual_cwd();

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

TEST_CASE("Util::to_lowercase")
{
  CHECK(Util::to_lowercase("") == "");
  CHECK(Util::to_lowercase("x") == "x");
  CHECK(Util::to_lowercase("X") == "x");
  CHECK(Util::to_lowercase(" x_X@") == " x_x@");
}

TEST_CASE("Util::traverse")
{
  TestContext test_context;

  REQUIRE(Util::create_dir("dir-with-subdir-and-file/subdir"));
  util::write_file("dir-with-subdir-and-file/subdir/f", "");
  REQUIRE(Util::create_dir("dir-with-files"));
  util::write_file("dir-with-files/f1", "");
  util::write_file("dir-with-files/f2", "");
  REQUIRE(Util::create_dir("empty-dir"));

  std::vector<std::string> visited;
  auto visitor = [&visited](const std::string& path, bool is_dir) {
    visited.push_back(FMT("[{}] {}", is_dir ? 'd' : 'f', path));
  };

  SUBCASE("traverse nonexistent path")
  {
    CHECK_THROWS_WITH(
      Util::traverse("nonexistent", visitor),
      "failed to open directory nonexistent: No such file or directory");
  }

  SUBCASE("traverse file")
  {
    CHECK_NOTHROW(Util::traverse("dir-with-subdir-and-file/subdir/f", visitor));
    REQUIRE(visited.size() == 1);
    CHECK(visited[0] == "[f] dir-with-subdir-and-file/subdir/f");
  }

  SUBCASE("traverse empty directory")
  {
    CHECK_NOTHROW(Util::traverse("empty-dir", visitor));
    REQUIRE(visited.size() == 1);
    CHECK(visited[0] == "[d] empty-dir");
  }

  SUBCASE("traverse directory with files")
  {
    CHECK_NOTHROW(Util::traverse("dir-with-files", visitor));
    REQUIRE(visited.size() == 3);
    std::string f1 = os_path("[f] dir-with-files/f1");
    std::string f2 = os_path("[f] dir-with-files/f2");
    CHECK(((visited[0] == f1 && visited[1] == f2)
           || (visited[0] == f2 && visited[1] == f1)));
    CHECK(visited[2] == "[d] dir-with-files");
  }

  SUBCASE("traverse directory hierarchy")
  {
    CHECK_NOTHROW(Util::traverse("dir-with-subdir-and-file", visitor));
    REQUIRE(visited.size() == 3);
    CHECK(visited[0] == os_path("[f] dir-with-subdir-and-file/subdir/f"));
    CHECK(visited[1] == os_path("[d] dir-with-subdir-and-file/subdir"));
    CHECK(visited[2] == "[d] dir-with-subdir-and-file");
  }
}

TEST_CASE("Util::wipe_path")
{
  TestContext test_context;

  SUBCASE("Wipe nonexistent path")
  {
    CHECK_NOTHROW(Util::wipe_path("a"));
  }

  SUBCASE("Wipe file")
  {
    util::write_file("a", "");
    CHECK_NOTHROW(Util::wipe_path("a"));
    CHECK(!Stat::stat("a"));
  }

  SUBCASE("Wipe directory")
  {
    REQUIRE(Util::create_dir("a/b"));
    util::write_file("a/1", "");
    util::write_file("a/b/1", "");
    CHECK_NOTHROW(Util::wipe_path("a"));
    CHECK(!Stat::stat("a"));
  }

  SUBCASE("Wipe bad path")
  {
#ifdef _WIN32
    const char error[] = "failed to rmdir .: Permission denied";
#elif defined(_AIX)
    const char error[] = "failed to rmdir .: Device busy";
#else
    const char error[] = "failed to rmdir .: Invalid argument";
#endif
    CHECK_THROWS_WITH(Util::wipe_path("."), error);
  }
}

TEST_SUITE_END();
