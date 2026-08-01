// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
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

#include <ccache/compiler/clang.hpp>

#include <doctest/doctest.h>

#include <string>

TEST_SUITE_BEGIN("clang");

TEST_CASE("compiler::split_preprocessed_output_from_clang_cuda")
{
  SUBCASE("normal")
  {
    const std::string output = R"(# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
void caller() {
  add<<<10000, 1>>>(__null,__null);
}
# 1 "test_cuda.cu"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
)";

    auto result = compiler::split_preprocessed_output_from_clang_cuda(output);

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

  SUBCASE("empty output")
  {
    CHECK(compiler::split_preprocessed_output_from_clang_cuda("").empty());
  }
}

TEST_SUITE_END();
