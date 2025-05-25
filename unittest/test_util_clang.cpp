// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/util/clang.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <string>
#include <vector>

TEST_SUITE_BEGIN("util");

// RAII helper class for test file management
class TestFileGuard
{
private:
  std::string filename_;

public:
  explicit TestFileGuard(const std::string& filename) : filename_(filename)
  {
  }

  ~TestFileGuard()
  {
    try {
      if (std::filesystem::exists(filename_)) {
        std::filesystem::remove(filename_);
      }
    } catch (...) {
      // Ignore cleanup errors in destructor
    }
  }

  void
  create_file(const std::string& content)
  {
    std::ofstream outfile;
    outfile.open(filename_);
    if (outfile.is_open()) {
      outfile << content;
      outfile.close();
    }
  }

  const std::string&
  filename() const
  {
    return filename_;
  }
};

TEST_CASE("util::split_preprocess_file_in_clang_cuda")
{
  SUBCASE("normal")
  {
    TestFileGuard guard("test_normal.txt");
    std::string content = R"(# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
void caller() {
  add<<<10000, 1>>>(__null,__null);
}
# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
)";
    guard.create_file(content);

    auto result = util::split_preprocess_file_in_clang_cuda(guard.filename());

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
    std::string nonexistent_file = "nonexistent_file_12345.txt";

    auto result = util::split_preprocess_file_in_clang_cuda(nonexistent_file);

    CHECK(result.empty());
  }

  SUBCASE("empty file")
  {
    TestFileGuard guard("test_empty.txt");
    guard.create_file("");

    auto result = util::split_preprocess_file_in_clang_cuda(guard.filename());

    CHECK(result.empty());
  }
}

TEST_SUITE_END();
