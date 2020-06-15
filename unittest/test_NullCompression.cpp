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

TEST_CASE("Compression::Type::none roundtrip")
{
  TestContext test_context;

  File f("data.uncompressed", "w");
  auto compressor =
    Compressor::create_from_type(Compression::Type::none, f.get(), 1);
  CHECK(compressor->actual_compression_level() == 0);
  compressor->write("foobar", 6);
  compressor->finalize();

  f.open("data.uncompressed", "r");
  auto decompressor =
    Decompressor::create_from_type(Compression::Type::none, f.get());

  char buffer[4];
  decompressor->read(buffer, 4);
  CHECK(memcmp(buffer, "foob", 4) == 0);

  SECTION("Garbage data")
  {
    // Not reached the end.
    CHECK_THROWS_WITH(decompressor->finalize(),
                      Equals("garbage data at end of uncompressed stream"));
  }

  SECTION("Read to end")
  {
    decompressor->read(buffer, 2);
    CHECK(memcmp(buffer, "ar", 2) == 0);

    // Reached the end.
    decompressor->finalize();

    // Nothing left to read.
    CHECK_THROWS_WITH(decompressor->read(buffer, 1),
                      Equals("failed to read from uncompressed stream"));
  }
}
