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

#include "../src/Compression.hpp"
#include "../src/Compressor.hpp"
#include "../src/Decompressor.hpp"
#include "../src/File.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

using Catch::Equals;
using TestUtil::TestContext;

TEST_CASE("Small Compression::Type::zstd roundtrip")
{
  TestContext test_context;

  File f("data.zstd", "wb");
  auto compressor =
    Compressor::create_from_type(Compression::Type::zstd, f.get(), 1);
  CHECK(compressor->actual_compression_level() == 1);
  compressor->write("foobar", 6);
  compressor->finalize();

  f.open("data.zstd", "rb");
  auto decompressor =
    Decompressor::create_from_type(Compression::Type::zstd, f.get());

  char buffer[4];
  decompressor->read(buffer, 4);
  CHECK(memcmp(buffer, "foob", 4) == 0);

  // Not reached the end.
  CHECK_THROWS_WITH(decompressor->finalize(),
                    Equals("garbage data at end of zstd input stream"));

  decompressor->read(buffer, 2);
  CHECK(memcmp(buffer, "ar", 2) == 0);

  // Reached the end.
  decompressor->finalize();

  // Nothing left to read.
  CHECK_THROWS_WITH(decompressor->read(buffer, 1),
                    Equals("failed to read from zstd input stream"));
}

TEST_CASE("Large compressible Compression::Type::zstd roundtrip")
{
  TestContext test_context;

  char data[] = "The quick brown fox jumps over the lazy dog";

  File f("data.zstd", "wb");
  auto compressor =
    Compressor::create_from_type(Compression::Type::zstd, f.get(), 1);
  for (size_t i = 0; i < 1000; i++) {
    compressor->write(data, sizeof(data));
  }
  compressor->finalize();

  f.open("data.zstd", "rb");
  auto decompressor =
    Decompressor::create_from_type(Compression::Type::zstd, f.get());

  char buffer[sizeof(data)];
  for (size_t i = 0; i < 1000; i++) {
    decompressor->read(buffer, sizeof(buffer));
    CHECK(memcmp(buffer, data, sizeof(data)) == 0);
  }

  // Reached the end.
  decompressor->finalize();

  // Nothing left to read.
  CHECK_THROWS_WITH(decompressor->read(buffer, 1),
                    Equals("failed to read from zstd input stream"));
}

TEST_CASE("Large uncompressible Compression::Type::zstd roundtrip")
{
  TestContext test_context;

  char data[100000];
  for (char& c : data) {
    c = rand() % 256;
  }

  File f("data.zstd", "wb");
  auto compressor =
    Compressor::create_from_type(Compression::Type::zstd, f.get(), 1);
  compressor->write(data, sizeof(data));
  compressor->finalize();

  f.open("data.zstd", "rb");
  auto decompressor =
    Decompressor::create_from_type(Compression::Type::zstd, f.get());

  char buffer[sizeof(data)];
  decompressor->read(buffer, sizeof(buffer));
  CHECK(memcmp(buffer, data, sizeof(data)) == 0);

  decompressor->finalize();
}
