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
          == "3345524abf6bbe1809449224b5972c41790b6cf2");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(h, "a");
    CHECK(hash_result(h).to_string()
          == "948caa2db61bc4cdb4faf7740cd491f195043914");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(h, "message digest");
    CHECK(hash_result(h).to_string()
          == "6bfec6f65e52962be863d6ea1005fc5e4cc8478c");
    hash_free(h);
  }

  {
    struct hash* h = hash_init();
    hash_string(
      h,
      "1234567890123456789012345678901234567890123456789012345678901234567890"
      "1"
      "234567890");
    CHECK(hash_result(h).to_string()
          == "c2be0e534a67d25947f0c7e78527b2f82abd260f");
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
        == "6bfec6f65e52962be863d6ea1005fc5e4cc8478c");
  hash_free(h);
}

TEST_CASE("hash_result_should_be_idempotent")
{
  struct hash* h = hash_init();
  hash_string(h, "");
  hash_result(h);
  CHECK(hash_result(h).to_string()
        == "3345524abf6bbe1809449224b5972c41790b6cf2");
  CHECK(hash_result(h).to_string()
        == "3345524abf6bbe1809449224b5972c41790b6cf2");
  hash_free(h);
}

TEST_CASE("hash_result digest bytes")
{
  struct hash* h = hash_init();
  hash_string(h, "message digest");
  Digest d = hash_result(h);
  uint8_t expected[Digest::size()] = {
    0x6b, 0xfe, 0xc6, 0xf6, 0x5e, 0x52, 0x96, 0x2b, 0xe8, 0x63,
    0xd6, 0xea, 0x10, 0x05, 0xfc, 0x5e, 0x4c, 0xc8, 0x47, 0x8c,
  };
  CHECK(memcmp(d.bytes(), expected, sizeof(Digest::size())) == 0);
  hash_free(h);
}
