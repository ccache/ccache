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

#include "../src/hash.hpp"

#include "third_party/catch.hpp"

TEST_CASE("test_known_strings")
{
  {
    struct hash* h = hash_init();
    hash_string(h, "");
    CHECK(hash_result(h).to_string()
          == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(h, "a");
    CHECK(hash_result(h).to_string()
          == "17762fddd969a453925d65717ac3eea21320b66b");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(h, "message digest");
    CHECK(hash_result(h).to_string()
          == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(
      h,
      "1234567890123456789012345678901234567890123456789012345678901234567890"
      "1234567890");
    CHECK(hash_result(h).to_string()
          == "f263acf51621980b9c8de5da4a17d314984e05ab");
    hash_free(h);
  }
}

TEST_CASE("hash_result_should_not_alter_state")
{
  struct hash* h = hash_init();
  hash_string(h, "message");
  hash_result(h);
  hash_string(h, " digest");
  CHECK(hash_result(h).to_string()
        == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
  hash_free(h);
}

TEST_CASE("hash_result_should_be_idempotent")
{
  struct hash* h = hash_init();
  hash_string(h, "");
  hash_result(h);
  CHECK(hash_result(h).to_string()
        == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  CHECK(hash_result(h).to_string()
        == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  hash_free(h);
}

TEST_CASE("hash_result digest bytes")
{
  struct hash* h = hash_init();
  hash_string(h, "message digest");
  Digest d = hash_result(h);
  uint8_t expected[Digest::size()] = {
    0x7b, 0xc2, 0xa2, 0xee, 0xb9, 0x5d, 0xdb, 0xf9, 0xb7, 0xec,
    0xf6, 0xad, 0xcb, 0x76, 0xb4, 0x53, 0x09, 0x1c, 0x58, 0xdc,
  };
  CHECK(memcmp(d.bytes(), expected, sizeof(Digest::size())) == 0);
  hash_free(h);
}

TEST_CASE("hash_once")
{
  CHECK(hash_buffer_once("a", 1).to_string()
        == "17762fddd969a453925d65717ac3eea21320b66b");
}
