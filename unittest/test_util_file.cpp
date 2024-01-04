// Copyright (C) 2022-2023 Joel Rosdahl and other contributors
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

#include <util/Bytes.hpp>
#include <util/DirEntry.hpp>
#include <util/Fd.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/string.hpp>

#include <third_party/doctest.h>

#include <fcntl.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fs = util::filesystem;

using TestUtil::TestContext;
using util::DirEntry;

TEST_CASE("util::fallocate")
{
  TestContext test_context;

  const char filename[] = "test-file";

  CHECK(
    util::fallocate(util::Fd(creat(filename, S_IRUSR | S_IWUSR)).get(), 10000));
  CHECK(DirEntry(filename).size() == 10000);

  CHECK(util::fallocate(
    util::Fd(open(filename, O_RDWR, S_IRUSR | S_IWUSR)).get(), 5000));
  CHECK(DirEntry(filename).size() == 10000);

  CHECK(util::fallocate(
    util::Fd(open(filename, O_RDWR, S_IRUSR | S_IWUSR)).get(), 20000));
  CHECK(DirEntry(filename).size() == 20000);
}

TEST_CASE("util::likely_size_on_disk")
{
  CHECK(util::likely_size_on_disk(0) == 0);
  CHECK(util::likely_size_on_disk(1) == 4096);
  CHECK(util::likely_size_on_disk(4095) == 4096);
  CHECK(util::likely_size_on_disk(4096) == 4096);
  CHECK(util::likely_size_on_disk(4097) == 8192);
}

TEST_CASE("util::read_file and util::write_file, text data")
{
  TestContext test_context;

  REQUIRE(util::write_file("test", "foo\nbar\n"));
  auto data = util::read_file<std::string>("test");
  REQUIRE(data);
  CHECK(*data == "foo\nbar\n");

  REQUIRE(util::write_file("test", "foo\r\nbar\r\n"));
  data = util::read_file<std::string>("test");
  REQUIRE(data);
  CHECK(*data == "foo\r\nbar\r\n");

  // Newline handling
  REQUIRE(util::write_file("test", "foo\r\nbar\n"));
  auto bin_data = util::read_file<std::vector<uint8_t>>("test");
  REQUIRE(bin_data);
#ifdef _WIN32
  const std::string expected_bin_data = "foo\r\r\nbar\r\n";
#else
  const std::string expected_bin_data = "foo\r\nbar\n";
#endif
  CHECK(*bin_data
        == std::vector<uint8_t>(expected_bin_data.begin(),
                                expected_bin_data.end()));

  REQUIRE(util::write_file("size_hint_test", std::string(8192, '\0')));
  data = util::read_file<std::string>("size_hint_test", 8191 /*size_hint*/);
  REQUIRE(data);
  CHECK(data->size() == 8192);
  data = util::read_file<std::string>("size_hint_test", 8193 /*size_hint*/);
  REQUIRE(data);
  CHECK(data->size() == 8192);

  data = util::read_file<std::string>("does/not/exist");
  REQUIRE(!data);
  CHECK(data.error() == "No such file or directory");

  auto result = util::write_file("", "does/not/exist");
  REQUIRE(!result);
  CHECK(result.error() == "No such file or directory");

  result = util::write_file("does/not/exist", "does/not/exist");
  REQUIRE(!result);
  CHECK(result.error() == "No such file or directory");
}

TEST_CASE("util::read_file and util::write_file, binary data")
{
  TestContext test_context;

  std::vector<uint8_t> expected;
  for (size_t i = 0; i < 512; ++i) {
    expected.push_back(static_cast<uint8_t>((32 + i) % 256));
  }

  CHECK(util::write_file("test", expected));
  auto actual = util::read_file<std::vector<uint8_t>>("test");
  REQUIRE(actual);
  CHECK(*actual == expected);

  REQUIRE(util::write_file("size_hint_test", std::vector<uint8_t>(8192, 0)));
  auto data =
    util::read_file<std::vector<uint8_t>>("size_hint_test", 8191 /*size_hint*/);
  REQUIRE(data);
  CHECK(data->size() == 8192);
  data =
    util::read_file<std::vector<uint8_t>>("size_hint_test", 8193 /*size_hint*/);
  REQUIRE(data);
  CHECK(data->size() == 8192);
}

#ifdef _WIN32
TEST_CASE("util::read_file<std::string> with UTF-16 little endian encoding")
{
  TestContext test_context;

  std::string data;
  data.push_back(static_cast<unsigned char>(0xff));
  data.push_back(static_cast<unsigned char>(0xfe));
  data.push_back('a');
  data.push_back('\0');
  data.push_back('b');
  data.push_back('\0');
  data.push_back('c');
  data.push_back('\0');

  CHECK(util::write_file("test", data));
  auto read_data = util::read_file<std::string>("test");
  REQUIRE(read_data);
  CHECK(*read_data == "abc");

  data.push_back('\0');
  data.push_back(static_cast<unsigned char>(0xd8));
  data.push_back('d');
  data.push_back('\0');
  CHECK(util::write_file("test", data));
  read_data = util::read_file<std::string>("test");
  REQUIRE(!read_data);
}
#endif

TEST_CASE("util::read_file_part")
{
  CHECK(util::write_file("test", "banana"));

  SUBCASE("util::Bytes")
  {
    CHECK(util::read_file_part<util::Bytes>("test", 0, 0) == util::to_span(""));
    CHECK(util::read_file_part<util::Bytes>("test", 0, 6)
          == util::to_span("banana"));
    CHECK(util::read_file_part<util::Bytes>("test", 0, 1000)
          == util::to_span("banana"));

    CHECK(util::read_file_part<util::Bytes>("test", 3, 0) == util::to_span(""));
    CHECK(util::read_file_part<util::Bytes>("test", 3, 2)
          == util::to_span("an"));
    CHECK(util::read_file_part<util::Bytes>("test", 3, 1000)
          == util::to_span("ana"));

    CHECK(util::read_file_part<util::Bytes>("test", 1000, 1000)
          == util::to_span(""));
  }
  SUBCASE("std::vector<uint8_t>")
  {
    auto data = util::read_file_part<std::vector<uint8_t>>("test", 3, 2);
    CHECK(*data == std::vector<uint8_t>{'a', 'n'});
  }

  SUBCASE("std::string")
  {
    auto data = util::read_file_part<std::string>("test", 3, 2);
    CHECK(*data == "an");
  }
}

TEST_CASE("util::traverse_directory")
{
  TestContext test_context;

  REQUIRE(fs::create_directories("dir-with-subdir-and-file/subdir"));
  util::write_file("dir-with-subdir-and-file/subdir/f", "");
  REQUIRE(fs::create_directory("dir-with-files"));
  util::write_file("dir-with-files/f1", "");
  util::write_file("dir-with-files/f2", "");
  REQUIRE(fs::create_directory("empty-dir"));

  std::vector<std::string> visited;
  auto visitor = [&visited](const auto& de) {
    visited.push_back(FMT("[{}] {}", de.is_directory() ? 'd' : 'f', de.path()));
  };

  SUBCASE("traverse nonexistent path")
  {
    CHECK(util::traverse_directory("nonexistent", visitor).error()
          == "Failed to traverse nonexistent: No such file or directory");
    CHECK(visited.size() == 0);
  }

  SUBCASE("traverse file")
  {
    CHECK(util::traverse_directory("dir-with-subdir-and-file/subdir/f", visitor)
            .error()
          == "Failed to traverse dir-with-subdir-and-file/subdir/f: Not a directory");
    CHECK(visited.size() == 0);
  }

  SUBCASE("traverse empty directory")
  {
    CHECK_NOTHROW(util::traverse_directory("empty-dir", visitor));
    REQUIRE(visited.size() == 1);
    CHECK(visited[0] == "[d] empty-dir");
  }

  SUBCASE("traverse directory with files")
  {
    CHECK_NOTHROW(util::traverse_directory("dir-with-files", visitor));
    REQUIRE(visited.size() == 3);
    fs::path f1("[f] dir-with-files/f1");
    fs::path f2("[f] dir-with-files/f2");
    CHECK(((visited[0] == f1 && visited[1] == f2)
           || (visited[0] == f2 && visited[1] == f1)));
    CHECK(visited[2] == "[d] dir-with-files");
  }

  SUBCASE("traverse directory hierarchy")
  {
    CHECK_NOTHROW(
      util::traverse_directory("dir-with-subdir-and-file", visitor));
    REQUIRE(visited.size() == 3);
    CHECK(visited[0] == fs::path("[f] dir-with-subdir-and-file/subdir/f"));
    CHECK(visited[1] == fs::path("[d] dir-with-subdir-and-file/subdir"));
    CHECK(visited[2] == "[d] dir-with-subdir-and-file");
  }
}
