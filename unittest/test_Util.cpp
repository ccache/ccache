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

#include "../src/Util.hpp"

#include "third_party/catch.hpp"

using Catch::Equals;

TEST_CASE("Util::base_name")
{
  CHECK(Util::base_name("") == "");
  CHECK(Util::base_name(".") == ".");
  CHECK(Util::base_name("foo") == "foo");
  CHECK(Util::base_name("/") == "");
  CHECK(Util::base_name("/foo") == "foo");
  CHECK(Util::base_name("/foo/bar/f.txt") == "f.txt");
}

TEST_CASE("Util::big_endian_to_int")
{
  uint8_t bytes[8] = {0x70, 0x9e, 0x9a, 0xbc, 0xd6, 0x54, 0x4b, 0xca};

  uint8_t uint8;
  Util::big_endian_to_int(bytes, uint8);
  CHECK(uint8 == 0x70);

  int8_t int8;
  Util::big_endian_to_int(bytes, int8);
  CHECK(int8 == 0x70);

  uint16_t uint16;
  Util::big_endian_to_int(bytes, uint16);
  CHECK(uint16 == 0x709e);

  int16_t int16;
  Util::big_endian_to_int(bytes, int16);
  CHECK(int16 == 0x709e);

  uint32_t uint32;
  Util::big_endian_to_int(bytes, uint32);
  CHECK(uint32 == 0x709e9abc);

  int32_t int32;
  Util::big_endian_to_int(bytes, int32);
  CHECK(int32 == 0x709e9abc);

  uint64_t uint64;
  Util::big_endian_to_int(bytes, uint64);
  CHECK(uint64 == 0x709e9abcd6544bca);

  int64_t int64;
  Util::big_endian_to_int(bytes, int64);
  CHECK(int64 == 0x709e9abcd6544bca);
}

TEST_CASE("Util::create_dir")
{
  CHECK(Util::create_dir("/"));

  CHECK(Util::create_dir("create/dir"));
  struct stat st;
  CHECK(stat("create/dir", &st) == 0);
  CHECK(S_ISDIR(st.st_mode));

  Util::write_file("create/dir/file", "");
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
}

TEST_CASE("Util::ends_with")
{
  CHECK(Util::ends_with("", ""));
  CHECK(Util::ends_with("x", ""));
  CHECK(Util::ends_with("x", "x"));
  CHECK(Util::ends_with("xy", ""));
  CHECK(Util::ends_with("xy", "y"));
  CHECK(Util::ends_with("xy", "xy"));
  CHECK(Util::ends_with("xyz", ""));
  CHECK(Util::ends_with("xyz", "z"));
  CHECK(Util::ends_with("xyz", "yz"));
  CHECK(Util::ends_with("xyz", "xyz"));

  CHECK_FALSE(Util::ends_with("", "x"));
  CHECK_FALSE(Util::ends_with("x", "y"));
  CHECK_FALSE(Util::ends_with("x", "xy"));
}

TEST_CASE("Util::for_each_level_1_subdir")
{
  std::vector<std::string> actual;
  Util::for_each_level_1_subdir(
    "cache_dir",
    [&](const std::string& subdir, const Util::ProgressReceiver&) {
      actual.push_back(subdir);
    },
    [](double) {});

  std::vector<std::string> expected = {
    "cache_dir/0",
    "cache_dir/1",
    "cache_dir/2",
    "cache_dir/3",
    "cache_dir/4",
    "cache_dir/5",
    "cache_dir/6",
    "cache_dir/7",
    "cache_dir/8",
    "cache_dir/9",
    "cache_dir/a",
    "cache_dir/b",
    "cache_dir/c",
    "cache_dir/d",
    "cache_dir/e",
    "cache_dir/f",
  };
  CHECK(actual == expected);
}

TEST_CASE("Util::get_file_size")
{
  uint64_t size;
  CHECK(!Util::get_file_size("does not exist", size));

  Util::write_file("foo", "foo");
  CHECK(Util::get_file_size("foo", size));
  CHECK(size == 3);
}

TEST_CASE("Util::get_level_1_files")
{
  Util::create_dir("e/m/p/t/y");

  Util::create_dir("0/1");
  Util::create_dir("0/f/c");
  Util::write_file("0/file_a", "");
  Util::write_file("0/1/file_b", "1");
  Util::write_file("0/1/file_c", "12");
  Util::write_file("0/f/c/file_d", "123");

  std::vector<std::shared_ptr<CacheFile>> files;
  auto null_receiver = [](double) {};

  SECTION("nonexistent subdirectory")
  {
    Util::get_level_1_files("2", null_receiver, files);
    CHECK(files.empty());
  }

  SECTION("empty subdirectory")
  {
    Util::get_level_1_files("e", null_receiver, files);
    CHECK(files.empty());
  }

  SECTION("simple case")
  {
    Util::get_level_1_files("0", null_receiver, files);
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

TEST_CASE("Util::int_to_big_endian")
{
  uint8_t bytes[8];

  uint8_t uint8 = 0x70;
  Util::int_to_big_endian(uint8, bytes);
  CHECK(bytes[0] == 0x70);

  int8_t int8 = 0x70;
  Util::int_to_big_endian(int8, bytes);
  CHECK(bytes[0] == 0x70);

  uint16_t uint16 = 0x709e;
  Util::int_to_big_endian(uint16, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);

  int16_t int16 = 0x709e;
  Util::int_to_big_endian(int16, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);

  uint32_t uint32 = 0x709e9abc;
  Util::int_to_big_endian(uint32, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);

  int32_t int32 = 0x709e9abc;
  Util::int_to_big_endian(int32, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);

  uint64_t uint64 = 0x709e9abcd6544bca;
  Util::int_to_big_endian(uint64, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);
  CHECK(bytes[4] == 0xd6);
  CHECK(bytes[5] == 0x54);
  CHECK(bytes[6] == 0x4b);
  CHECK(bytes[7] == 0xca);

  int64_t int64 = 0x709e9abcd6544bca;
  Util::int_to_big_endian(int64, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);
  CHECK(bytes[4] == 0xd6);
  CHECK(bytes[5] == 0x54);
  CHECK(bytes[6] == 0x4b);
  CHECK(bytes[7] == 0xca);
}

TEST_CASE("Util::parse_int")
{
  CHECK(Util::parse_int("0") == 0);
  CHECK(Util::parse_int("2") == 2);
  CHECK(Util::parse_int("-17") == -17);
  CHECK(Util::parse_int("42") == 42);
  CHECK(Util::parse_int("0666") == 666);
  CHECK(Util::parse_int(" 777") == 777);

  CHECK_THROWS_WITH(Util::parse_int(""), Equals("invalid integer: \"\""));
  CHECK_THROWS_WITH(Util::parse_int("x"), Equals("invalid integer: \"x\""));
  CHECK_THROWS_WITH(Util::parse_int("0x"), Equals("invalid integer: \"0x\""));
  CHECK_THROWS_WITH(Util::parse_int("0x4"), Equals("invalid integer: \"0x4\""));
  CHECK_THROWS_WITH(Util::parse_int("0 "), Equals("invalid integer: \"0 \""));
}

TEST_CASE("Util::read_file and Util::write_file")
{
  Util::write_file("test", "foo\nbar\n");
  std::string data = Util::read_file("test");
  CHECK(data == "foo\nbar\n");
}

TEST_CASE("Util::starts_with")
{
  CHECK(Util::starts_with("", ""));
  CHECK(Util::starts_with("x", ""));
  CHECK(Util::starts_with("x", "x"));
  CHECK(Util::starts_with("xy", ""));
  CHECK(Util::starts_with("xy", "x"));
  CHECK(Util::starts_with("xy", "xy"));
  CHECK(Util::starts_with("xyz", ""));
  CHECK(Util::starts_with("xyz", "x"));
  CHECK(Util::starts_with("xyz", "xy"));
  CHECK(Util::starts_with("xyz", "xyz"));

  CHECK_FALSE(Util::starts_with("", "x"));
  CHECK_FALSE(Util::starts_with("x", "y"));
  CHECK_FALSE(Util::starts_with("x", "xy"));
}

TEST_CASE("Util::strip_whitespace")
{
  CHECK(Util::strip_whitespace("") == "");
  CHECK(Util::strip_whitespace("x") == "x");
  CHECK(Util::strip_whitespace(" x") == "x");
  CHECK(Util::strip_whitespace("x ") == "x");
  CHECK(Util::strip_whitespace(" x ") == "x");
  CHECK(Util::strip_whitespace(" \n\tx \n\t") == "x");
  CHECK(Util::strip_whitespace("  x  y  ") == "x  y");
}

TEST_CASE("Util::to_lowercase")
{
  CHECK(Util::to_lowercase("") == "");
  CHECK(Util::to_lowercase("x") == "x");
  CHECK(Util::to_lowercase("X") == "x");
  CHECK(Util::to_lowercase(" x_X@") == " x_x@");
}
