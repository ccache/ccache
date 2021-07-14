// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <util/expected.hpp>

#include <third_party/doctest.h>
#include <third_party/nonstd/expected.hpp>

#include <iostream> // macOS bug: https://github.com/onqtam/doctest/issues/126
#include <memory>
#include <stdexcept>
#include <string>

class TestException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

TEST_CASE("util::value_or_throw")
{
  using util::value_or_throw;

  SUBCASE("const ref")
  {
    const nonstd::expected<int, const char*> with_value = 42;
    const nonstd::expected<int, const char*> without_value =
      nonstd::make_unexpected("no value");

    CHECK(value_or_throw<TestException>(with_value) == 42);
    CHECK_THROWS_WITH(value_or_throw<TestException>(without_value), "no value");
  }

  SUBCASE("move")
  {
    const std::string value = "value";
    nonstd::expected<std::unique_ptr<std::string>, const char*> with_value =
      std::make_unique<std::string>(value);
    CHECK(*value_or_throw<TestException>(std::move(with_value)) == value);
  }
}
