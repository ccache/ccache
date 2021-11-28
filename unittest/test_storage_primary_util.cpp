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

#include "TestUtil.hpp"

#include <Util.hpp>
#include <storage/primary/util.hpp>

#include <third_party/doctest.h>

#include <string>

using TestUtil::TestContext;

static inline std::string
os_path(std::string path)
{
#if defined(_WIN32) && !defined(HAVE_DIRENT_H)
  std::replace(path.begin(), path.end(), '/', '\\');
#endif

  return path;
}

TEST_SUITE_BEGIN("storage::primary::util");

TEST_CASE("storage::primary::for_each_level_1_subdir")
{
  std::vector<std::string> actual;
  storage::primary::for_each_level_1_subdir(
    "cache_dir",
    [&](const auto& subdir, const auto&) { actual.push_back(subdir); },
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

TEST_CASE("storage::primary::get_level_1_files")
{
  TestContext test_context;

  Util::create_dir("e/m/p/t/y");

  Util::create_dir("0/1");
  Util::create_dir("0/f/c");
  Util::write_file("0/file_a", "");
  Util::write_file("0/1/file_b", "1");
  Util::write_file("0/1/file_c", "12");
  Util::write_file("0/f/c/file_d", "123");

  auto null_receiver = [](double) {};

  SUBCASE("nonexistent subdirectory")
  {
    const auto files = storage::primary::get_level_1_files("2", null_receiver);
    CHECK(files.empty());
  }

  SUBCASE("empty subdirectory")
  {
    const auto files = storage::primary::get_level_1_files("e", null_receiver);
    CHECK(files.empty());
  }

  SUBCASE("simple case")
  {
    auto files = storage::primary::get_level_1_files("0", null_receiver);
    REQUIRE(files.size() == 4);

    // Files within a level are in arbitrary order, sort them to be able to
    // verify them.
    std::sort(files.begin(), files.end(), [](const auto& f1, const auto& f2) {
      return f1.path() < f2.path();
    });

    CHECK(files[0].path() == os_path("0/1/file_b"));
    CHECK(files[0].lstat().size() == 1);
    CHECK(files[1].path() == os_path("0/1/file_c"));
    CHECK(files[1].lstat().size() == 2);
    CHECK(files[2].path() == os_path("0/f/c/file_d"));
    CHECK(files[2].lstat().size() == 3);
    CHECK(files[3].path() == os_path("0/file_a"));
    CHECK(files[3].lstat().size() == 0);
  }
}

TEST_SUITE_END();
