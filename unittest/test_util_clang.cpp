// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/util/clang.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>

#include <doctest/doctest.h>

#include <string>

namespace fs = util::filesystem;

using TestUtil::TestContext;

TEST_SUITE_BEGIN("util");

TEST_CASE("util::split_preprocessed_file_from_clang_cuda")
{
  TestContext test_context;

  SUBCASE("normal")
  {
    fs::path filename = "test_normal.txt";
    REQUIRE(util::write_file(filename, R"(# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
void caller() {
  add<<<10000, 1>>>(__null,__null);
}
# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
)"));

    auto result = util::split_preprocessed_file_from_clang_cuda(filename);

    REQUIRE(result.size() == 2);
    CHECK(result[0] == R"(# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
void caller() {
  add<<<10000, 1>>>(__null,__null);
}
)");
    CHECK(result[1] == R"(# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
)");
  }

  SUBCASE("non-existent file")
  {
    fs::path filename = "nonexistent_file.txt";
    CHECK(util::split_preprocessed_file_from_clang_cuda(filename).empty());
  }

  SUBCASE("empty file")
  {
    fs::path filename = "test_empty.txt";
    REQUIRE(util::write_file(filename, ""));

    CHECK(util::split_preprocessed_file_from_clang_cuda(filename).empty());
  }
}

TEST_SUITE_END();
