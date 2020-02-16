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
#include "../src/Config.hpp"

#include "third_party/catch.hpp"

using Catch::Equals;

TEST_CASE("Compression::level_from_config")
{
  Config config;
  CHECK(Compression::level_from_config(config) == 0);
}

TEST_CASE("Compression::type_from_config")
{
  Config config;
  CHECK(Compression::type_from_config(config) == Compression::Type::zstd);
}

TEST_CASE("Compression::type_from_int")
{
  CHECK(Compression::type_from_int(0) == Compression::Type::none);
  CHECK(Compression::type_from_int(1) == Compression::Type::zstd);
  CHECK_THROWS_WITH(Compression::type_from_int(2), Equals("Unknown type: 2"));
}

TEST_CASE("Compression::type_to_string")
{
  CHECK(Compression::type_to_string(Compression::Type::none) == "none");
  CHECK(Compression::type_to_string(Compression::Type::zstd) == "zstd");
}
