// Copyright (C) 2011-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/xxh3_64.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("util::XXH3_64");

TEST_CASE("util::XXH3_64")
{
  util::XXH3_64 checksum;
  CHECK(checksum.digest() == 0x2d06800538d394c2);

  checksum.update("foo", 3);
  CHECK(checksum.digest() == 0xab6e5f64077e7d8a);

  checksum.update("t", 1);
  CHECK(checksum.digest() == 0x3fd918aed1a9e7e4);

  checksum.reset();
  CHECK(checksum.digest() == 0x2d06800538d394c2);
}

TEST_SUITE_END();
