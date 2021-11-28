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

#include "../src/Config.hpp"

#include <compression/types.hpp>

#include "third_party/doctest.h"

TEST_SUITE_BEGIN("compression");

TEST_CASE("compression::level_from_config")
{
  Config config;
  CHECK(compression::level_from_config(config) == 0);
}

TEST_CASE("compression::type_from_config")
{
  Config config;
  CHECK(compression::type_from_config(config) == compression::Type::zstd);
}

TEST_CASE("compression::type_from_int")
{
  CHECK(compression::type_from_int(0) == compression::Type::none);
  CHECK(compression::type_from_int(1) == compression::Type::zstd);
  CHECK_THROWS_WITH(compression::type_from_int(2), "Unknown type: 2");
}

TEST_CASE("compression::type_to_string")
{
  CHECK(compression::type_to_string(compression::Type::none) == "none");
  CHECK(compression::type_to_string(compression::Type::zstd) == "zstd");
}

TEST_SUITE_END();
