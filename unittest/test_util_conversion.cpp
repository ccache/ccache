// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include <util/conversion.hpp>

#include <third_party/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <vector>

TEST_SUITE_BEGIN("util");

TEST_CASE("util::to_string")
{
  const uint8_t bytes[] = {'f', 'o', 'o'};
  const char str[] = "foo";

  CHECK(util::to_string(std::string(str)) == std::string(str));
  CHECK(util::to_string(std::string_view(str)) == std::string(str));
  CHECK(util::to_string(nonstd::span<const uint8_t>(bytes))
        == std::string(str));
  CHECK(util::to_string(util::Bytes(bytes, 3)) == std::string(str));
}

TEST_CASE("util::to_string_view")
{
  uint8_t bytes[] = {'f', 'o', 'o'};
  char str[] = "foo";

  CHECK(util::to_string_view(nonstd::span(bytes)) == std::string(str));
}

TEST_SUITE_END();
