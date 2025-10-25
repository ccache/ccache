// Copyright (C) 2010-2025 Joel Rosdahl and other contributors
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

#include <ccache/hash.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("Hash");

TEST_CASE("known strings")
{
  SUBCASE("initial state")
  {
    CHECK(util::format_base16(Hash().digest())
          == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  }

  SUBCASE("empty string")
  {
    CHECK(util::format_base16(Hash().hash("").digest())
          == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  }

  SUBCASE("a")
  {
    CHECK(util::format_base16(Hash().hash("a").digest())
          == "17762fddd969a453925d65717ac3eea21320b66b");
  }

  SUBCASE("message digest")
  {
    CHECK(util::format_base16(Hash().hash("message digest").digest())
          == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
  }

  SUBCASE("long string")
  {
    const char long_string[] =
      "123456789012345678901234567890123456789012345678901234567890"
      "12345678901234567890";
    CHECK(util::format_base16(Hash().hash(long_string).digest())
          == "f263acf51621980b9c8de5da4a17d314984e05ab");
  }
}

TEST_CASE("Hash::digest should not alter state")
{
  Hash h;
  h.hash("message");
  h.digest();
  h.hash(" digest");
  CHECK(util::format_base16(h.digest())
        == "7bc2a2eeb95ddbf9b7ecf6adcb76b453091c58dc");
}

TEST_CASE("Hash::digest should be idempotent")
{
  Hash h;
  CHECK(util::format_base16(h.digest())
        == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
  CHECK(util::format_base16(h.digest())
        == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9");
}

TEST_SUITE_END();
