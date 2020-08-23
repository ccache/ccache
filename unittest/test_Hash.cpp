// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "../src/Hash.hpp"

#include "third_party/doctest.h"

TEST_SUITE_BEGIN("Hash");

TEST_CASE("known strings")
{
  SUBCASE("initial state")
  {
    CHECK(Hash().digest().to_string()
          == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  }

  SUBCASE("empty string")
  {
    CHECK(Hash().hash("").digest().to_string()
          == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  }

  SUBCASE("a")
  {
    CHECK(Hash().hash("a").digest().to_string()
          == "17762fddd969a453925d65717ac3eea21320b66b");
  }

  SUBCASE("message digest")
  {
    CHECK(Hash().hash("message digest").digest().to_string()
          == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
  }

  SUBCASE("long string")
  {
    const char long_string[] =
      "123456789012345678901234567890123456789012345678901234567890"
      "12345678901234567890";
    CHECK(Hash().hash(long_string).digest().to_string()
          == "f263acf51621980b9c8de5da4a17d314984e05ab");
  }
}

TEST_CASE("Hash::digest should not alter state")
{
  Hash h;
  h.hash("message");
  h.digest();
  h.hash(" digest");
  CHECK(h.digest().to_string() == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
}

TEST_CASE("Hash::digest should be idempotent")
{
  Hash h;
  CHECK(h.digest().to_string() == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  CHECK(h.digest().to_string() == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
}

TEST_CASE("Digest::bytes")
{
  Digest d = Hash().hash("message digest").digest();
  uint8_t expected[Digest::size()] = {
    0x7b, 0xc2, 0xa2, 0xee, 0xb9, 0x5d, 0xdb, 0xf9, 0xb7, 0xec,
    0xf6, 0xad, 0xcb, 0x76, 0xb4, 0x53, 0x09, 0x1c, 0x58, 0xdc,
  };
  CHECK(memcmp(d.bytes(), expected, sizeof(Digest::size())) == 0);
}

TEST_SUITE_END();
