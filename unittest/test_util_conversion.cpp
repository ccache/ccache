// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/conversion.hpp>
#include <ccache/util/filesystem.hpp>

#include <doctest/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <vector>

namespace fs = util::filesystem;

TEST_SUITE_BEGIN("util");

TEST_CASE("util::big_endian_to_int")
{
  uint8_t bytes[8] = {0x70, 0x9e, 0x9a, 0xbc, 0xd6, 0x54, 0x4b, 0xca};

  uint8_t uint8;
  util::big_endian_to_int(bytes, uint8);
  CHECK(uint8 == 0x70);

  int8_t int8;
  util::big_endian_to_int(bytes, int8);
  CHECK(int8 == 0x70);

  uint16_t uint16;
  util::big_endian_to_int(bytes, uint16);
  CHECK(uint16 == 0x709e);

  int16_t int16;
  util::big_endian_to_int(bytes, int16);
  CHECK(int16 == 0x709e);

  uint32_t uint32;
  util::big_endian_to_int(bytes, uint32);
  CHECK(uint32 == 0x709e9abc);

  int32_t int32;
  util::big_endian_to_int(bytes, int32);
  CHECK(int32 == 0x709e9abc);

  uint64_t uint64;
  util::big_endian_to_int(bytes, uint64);
  CHECK(uint64 == 0x709e9abcd6544bca);

  int64_t int64;
  util::big_endian_to_int(bytes, int64);
  CHECK(int64 == 0x709e9abcd6544bca);
}

TEST_CASE("util::int_to_big_endian")
{
  uint8_t bytes[8];

  uint8_t uint8 = 0x70;
  util::int_to_big_endian(uint8, bytes);
  CHECK(bytes[0] == 0x70);

  int8_t int8 = 0x70;
  util::int_to_big_endian(int8, bytes);
  CHECK(bytes[0] == 0x70);

  uint16_t uint16 = 0x709e;
  util::int_to_big_endian(uint16, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);

  int16_t int16 = 0x709e;
  util::int_to_big_endian(int16, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);

  uint32_t uint32 = 0x709e9abc;
  util::int_to_big_endian(uint32, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);

  int32_t int32 = 0x709e9abc;
  util::int_to_big_endian(int32, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);

  uint64_t uint64 = 0x709e9abcd6544bca;
  util::int_to_big_endian(uint64, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);
  CHECK(bytes[4] == 0xd6);
  CHECK(bytes[5] == 0x54);
  CHECK(bytes[6] == 0x4b);
  CHECK(bytes[7] == 0xca);

  int64_t int64 = 0x709e9abcd6544bca;
  util::int_to_big_endian(int64, bytes);
  CHECK(bytes[0] == 0x70);
  CHECK(bytes[1] == 0x9e);
  CHECK(bytes[2] == 0x9a);
  CHECK(bytes[3] == 0xbc);
  CHECK(bytes[4] == 0xd6);
  CHECK(bytes[5] == 0x54);
  CHECK(bytes[6] == 0x4b);
  CHECK(bytes[7] == 0xca);
}

TEST_CASE("util::to_string")
{
  const uint8_t bytes[] = {'f', 'o', 'o'};
  const char str[] = "foo";

  CHECK(util::to_string(std::string(str)) == std::string(str));
  CHECK(util::to_string(std::string_view(str)) == std::string(str));
  CHECK(util::to_string(nonstd::span<const uint8_t>(bytes))
        == std::string(str));
  CHECK(util::to_string(util::Bytes(bytes, 3)) == std::string(str));
  CHECK(util::to_string(fs::path("foo/bar")) == std::string("foo/bar"));
}

TEST_CASE("util::to_string_view")
{
  uint8_t bytes[] = {'f', 'o', 'o'};
  char str[] = "foo";

  CHECK(util::to_string_view(nonstd::span(bytes)) == std::string(str));
}

TEST_SUITE_END();
