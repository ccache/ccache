// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include <ccache/config.hpp>
#include <ccache/core/types.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("compression");

TEST_CASE("compression_level_from_config")
{
  Config config;
  CHECK(core::compression_level_from_config(config) == 0);
}

TEST_CASE("compression_type_from_config")
{
  Config config;
  CHECK(core::compression_type_from_config(config)
        == core::CompressionType::zstd);
}

TEST_CASE("compression_type_from_int")
{
  CHECK(core::compression_type_from_int(0) == core::CompressionType::none);
  CHECK(core::compression_type_from_int(1) == core::CompressionType::zstd);
  CHECK_THROWS_WITH(core::compression_type_from_int(2), "Unknown type: 2");
}

TEST_CASE("to_string(CompressionType)")
{
  CHECK(core::to_string(core::CompressionType::none) == "none");
  CHECK(core::to_string(core::CompressionType::zstd) == "zstd");
}

TEST_SUITE_END();
