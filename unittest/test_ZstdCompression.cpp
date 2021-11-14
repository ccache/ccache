// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include "../src/File.hpp"
#include "TestUtil.hpp"

#include <compression/Compressor.hpp>
#include <compression/Decompressor.hpp>
#include <compression/types.hpp>
#include <core/FileReader.hpp>
#include <core/FileWriter.hpp>

#include "third_party/doctest.h"

#include <cstring>

using compression::Compressor;
using compression::Decompressor;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("ZstdCompression");

TEST_CASE("Small compression::Type::zstd roundtrip")
{
  TestContext test_context;

  File f("data.zstd", "wb");
  core::FileWriter fw(f.get());
  auto compressor =
    Compressor::create_from_type(compression::Type::zstd, fw, 1);
  CHECK(compressor->actual_compression_level() == 1);
  compressor->write("foobar", 6);
  compressor->finalize();

  f.open("data.zstd", "rb");
  core::FileReader fr(f.get());
  auto decompressor =
    Decompressor::create_from_type(compression::Type::zstd, fr);

  char buffer[4];
  decompressor->read(buffer, 4);
  CHECK(memcmp(buffer, "foob", 4) == 0);

  // Not reached the end.
  CHECK_THROWS_WITH(decompressor->finalize(),
                    "Garbage data at end of zstd input stream");

  decompressor->read(buffer, 2);
  CHECK(memcmp(buffer, "ar", 2) == 0);

  // Reached the end.
  decompressor->finalize();

  // Nothing left to read.
  CHECK_THROWS_WITH(decompressor->read(buffer, 1),
                    "Failed to read from file stream");
}

TEST_CASE("Large compressible compression::Type::zstd roundtrip")
{
  TestContext test_context;

  char data[] = "The quick brown fox jumps over the lazy dog";

  File f("data.zstd", "wb");
  core::FileWriter fw(f.get());
  auto compressor =
    Compressor::create_from_type(compression::Type::zstd, fw, 1);
  for (size_t i = 0; i < 1000; i++) {
    compressor->write(data, sizeof(data));
  }
  compressor->finalize();

  f.open("data.zstd", "rb");
  core::FileReader fr(f.get());
  auto decompressor =
    Decompressor::create_from_type(compression::Type::zstd, fr);

  char buffer[sizeof(data)];
  for (size_t i = 0; i < 1000; i++) {
    decompressor->read(buffer, sizeof(buffer));
    CHECK(memcmp(buffer, data, sizeof(data)) == 0);
  }

  // Reached the end.
  decompressor->finalize();

  // Nothing left to read.
  CHECK_THROWS_WITH(decompressor->read(buffer, 1),
                    "Failed to read from file stream");
}

TEST_CASE("Large uncompressible compression::Type::zstd roundtrip")
{
  TestContext test_context;

  char data[100000];
  for (char& c : data) {
    c = rand() % 256;
  }

  File f("data.zstd", "wb");
  core::FileWriter fw(f.get());
  auto compressor =
    Compressor::create_from_type(compression::Type::zstd, fw, 1);
  compressor->write(data, sizeof(data));
  compressor->finalize();

  f.open("data.zstd", "rb");
  core::FileReader fr(f.get());
  auto decompressor =
    Decompressor::create_from_type(compression::Type::zstd, fr);

  char buffer[sizeof(data)];
  decompressor->read(buffer, sizeof(buffer));
  CHECK(memcmp(buffer, data, sizeof(data)) == 0);

  decompressor->finalize();
}

TEST_SUITE_END();
