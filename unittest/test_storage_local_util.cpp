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

#include <ccache/storage/local/util.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using TestUtil::TestContext;

namespace fs = util::filesystem;

TEST_SUITE_BEGIN("storage::local::util");

TEST_CASE("storage::local::for_each_cache_subdir")
{
  std::vector<uint8_t> actual;
  storage::local::for_each_cache_subdir(
    [](double) {},
    [&](uint8_t index, const auto&) { actual.push_back(index); });

  std::vector<uint8_t> expected = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  CHECK(actual == expected);
}

TEST_CASE("storage::local::get_cache_dir_files")
{
  TestContext test_context;

  REQUIRE(fs::create_directories("e/m/p/t/y"));

  REQUIRE(fs::create_directories("0/1"));
  REQUIRE(fs::create_directories("0/f/c"));
  REQUIRE(util::write_file("0/file_a", ""));
  REQUIRE(util::write_file("0/1/file_b", "1"));
  REQUIRE(util::write_file("0/1/file_c", "12"));
  REQUIRE(util::write_file("0/f/c/file_d", "123"));

  SUBCASE("nonexistent subdirectory")
  {
    const auto files = storage::local::get_cache_dir_files("2");
    CHECK(files.empty());
  }

  SUBCASE("empty subdirectory")
  {
    const auto files = storage::local::get_cache_dir_files("e");
    CHECK(files.empty());
  }

  SUBCASE("simple case")
  {
    auto files = storage::local::get_cache_dir_files("0");
    REQUIRE(files.size() == 4);

    // Files within a level are in arbitrary order, sort them to be able to
    // verify them.
    std::sort(files.begin(), files.end(), [](const auto& f1, const auto& f2) {
      return f1.path() < f2.path();
    });

    CHECK(files[0].path() == "0/1/file_b");
    CHECK(files[0].size() == 1);
    CHECK(files[1].path() == "0/1/file_c");
    CHECK(files[1].size() == 2);
    CHECK(files[2].path() == "0/f/c/file_d");
    CHECK(files[2].size() == 3);
    CHECK(files[3].path() == "0/file_a");
    CHECK(files[3].size() == 0);
  }
}

TEST_SUITE_END();
