// Copyright (C) 2011-2019 Joel Rosdahl and other contributors
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

#include "../src/Checksum.hpp"

#include "third_party/catch.hpp"

TEST_CASE("Checksums")
{
  Checksum checksum;
  CHECK(checksum.digest() == 0xef46db3751d8e999);

  checksum.update("foo", 3);
  CHECK(checksum.digest() == 0x33bf00a859c4ba3f);

  checksum.update("t", 1);
  CHECK(checksum.digest() == 0xef78250064b0eea7);

  checksum.reset();
  CHECK(checksum.digest() == 0xef46db3751d8e999);
}
