// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/bytes.hpp>
#include <ccache/util/zstd.hpp>

#include <doctest/doctest.h>

#include <string>

using TestUtil::TestContext;

const util::Bytes compressed_ab{
  0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x02, 0x11, 0x00, 0x00, 0x61, 0x62};

TEST_CASE("util::zstd_compress")
{
  TestContext test_context;

  util::Bytes output{'x'};
  auto result = util::zstd_compress(util::Bytes{'a', 'b'}, output, 1);
  CHECK(result);
  CHECK(output.size() == 12);
  util::Bytes expected{'x'};
  expected.insert(expected.end(), compressed_ab.begin(), compressed_ab.end());
  CHECK(output == expected);
}

TEST_CASE("util::zstd_decompress")
{
  TestContext test_context;

  util::Bytes input = compressed_ab;
  util::Bytes output{'x'};
  auto result = util::zstd_decompress(input, output, 2);
  CHECK(result);
  CHECK(output == util::Bytes{'x', 'a', 'b'});
}

TEST_CASE("ZSTD roundtrip")
{
  TestContext test_context;

  const util::Bytes data{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  const size_t copies = 10000;
  util::Bytes original_input;
  for (size_t i = 0; i < copies; i++) {
    original_input.insert(original_input.end(), data.begin(), data.end());
  }

  util::Bytes output;
  auto result = util::zstd_compress(original_input, output, 1);
  CHECK(result);
  CHECK(output.size() < 100);

  util::Bytes decompressed_input;
  result =
    util::zstd_decompress(output, decompressed_input, copies * data.size());
  CHECK(result);
  CHECK(decompressed_input == original_input);
}
