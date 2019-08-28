// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "../src/util.hpp"

#include <catch.hpp>

TEST_CASE("util::base_name")
{
  CHECK(util::base_name("") == "");
  CHECK(util::base_name(".") == ".");
  CHECK(util::base_name("foo") == "foo");
  CHECK(util::base_name("/") == "");
  CHECK(util::base_name("/foo") == "foo");
  CHECK(util::base_name("/foo/bar/f.txt") == "f.txt");
}

TEST_CASE("util::create_dir")
{
  CHECK(util::create_dir("/"));

  CHECK(util::create_dir("create/dir"));
  struct stat st;
  CHECK(stat("create/dir", &st) == 0);
  CHECK(S_ISDIR(st.st_mode));

  util::write_file("create/dir/file", "");
  CHECK(!util::create_dir("create/dir/file"));
}

TEST_CASE("util::dir_name")
{
  CHECK(util::dir_name("") == ".");
  CHECK(util::dir_name(".") == ".");
  CHECK(util::dir_name("foo") == ".");
  CHECK(util::dir_name("/") == "/");
  CHECK(util::dir_name("/foo") == "/");
  CHECK(util::dir_name("/foo/bar/f.txt") == "/foo/bar");
}

TEST_CASE("util::ends_with")
{
  CHECK(util::ends_with("", ""));
  CHECK(util::ends_with("x", ""));
  CHECK(util::ends_with("x", "x"));
  CHECK(util::ends_with("xy", ""));
  CHECK(util::ends_with("xy", "y"));
  CHECK(util::ends_with("xy", "xy"));
  CHECK(util::ends_with("xyz", ""));
  CHECK(util::ends_with("xyz", "z"));
  CHECK(util::ends_with("xyz", "yz"));
  CHECK(util::ends_with("xyz", "xyz"));

  CHECK_FALSE(util::ends_with("", "x"));
  CHECK_FALSE(util::ends_with("x", "y"));
  CHECK_FALSE(util::ends_with("x", "xy"));
}

TEST_CASE("util::get_level_1_files")
{
  util::create_dir("e/m/p/t/y");

  util::create_dir("0/1");
  util::create_dir("0/f/c");
  util::write_file("0/file_a", "");
  util::write_file("0/1/file_b", "1");
  util::write_file("0/1/file_c", "12");
  util::write_file("0/f/c/file_d", "123");

  std::vector<std::shared_ptr<CacheFile>> files;
  auto null_receiver = [](double) {};

  SECTION("nonexistent subdirectory")
  {
    util::get_level_1_files("2", null_receiver, files);
    CHECK(files.empty());
  }

  SECTION("empty subdirectory")
  {
    util::get_level_1_files("e", null_receiver, files);
    CHECK(files.empty());
  }

  SECTION("simple case")
  {
    util::get_level_1_files("0", null_receiver, files);
    REQUIRE(files.size() == 4);

    // Files within a level are in arbitrary order, sort them to be able to
    // verify them.
    std::sort(files.begin(),
              files.end(),
              [](const std::shared_ptr<CacheFile>& f1,
                 const std::shared_ptr<CacheFile>& f2) {
                return f1->path() < f2->path();
              });

    CHECK(files[0]->path() == "0/1/file_b");
    CHECK(files[0]->stat().st_size == 1);
    CHECK(files[1]->path() == "0/1/file_c");
    CHECK(files[1]->stat().st_size == 2);
    CHECK(files[2]->path() == "0/f/c/file_d");
    CHECK(files[2]->stat().st_size == 3);
    CHECK(files[3]->path() == "0/file_a");
    CHECK(files[3]->stat().st_size == 0);
  }
}

TEST_CASE("util::read_file and util::write_file")
{
  util::write_file("test", "foo\nbar\n");
  std::string data = util::read_file("test");
  CHECK(data == "foo\nbar\n");
}

TEST_CASE("util::starts_with")
{
  CHECK(util::starts_with("", ""));
  CHECK(util::starts_with("x", ""));
  CHECK(util::starts_with("x", "x"));
  CHECK(util::starts_with("xy", ""));
  CHECK(util::starts_with("xy", "x"));
  CHECK(util::starts_with("xy", "xy"));
  CHECK(util::starts_with("xyz", ""));
  CHECK(util::starts_with("xyz", "x"));
  CHECK(util::starts_with("xyz", "xy"));
  CHECK(util::starts_with("xyz", "xyz"));

  CHECK_FALSE(util::starts_with("", "x"));
  CHECK_FALSE(util::starts_with("x", "y"));
  CHECK_FALSE(util::starts_with("x", "xy"));
}

TEST_CASE("util::strip_whitespace")
{
  CHECK(util::strip_whitespace("") == "");
  CHECK(util::strip_whitespace("x") == "x");
  CHECK(util::strip_whitespace(" x") == "x");
  CHECK(util::strip_whitespace("x ") == "x");
  CHECK(util::strip_whitespace(" x ") == "x");
  CHECK(util::strip_whitespace(" \n\tx \n\t") == "x");
  CHECK(util::strip_whitespace("  x  y  ") == "x  y");
}

TEST_CASE("util::to_lowercase")
{
  CHECK(util::to_lowercase("") == "");
  CHECK(util::to_lowercase("x") == "x");
  CHECK(util::to_lowercase("X") == "x");
  CHECK(util::to_lowercase(" x_X@") == " x_x@");
}
