// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/expected.hpp>

#include <doctest/doctest.h>
#include <tl/expected.hpp>

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
  using util::throw_on_error;
  using util::value_or_throw;

  SUBCASE("const ref")
  {
    const tl::expected<int, const char*> with_value = 42;
    const tl::expected<int, const char*> with_error =
      tl::unexpected("no value");

    CHECK(value_or_throw<TestException>(with_value) == 42);
    CHECK_THROWS_WITH(value_or_throw<TestException>(with_error), "no value");
  }

  SUBCASE("move")
  {
    const std::string value = "value";
    tl::expected<std::unique_ptr<std::string>, const char*> with_value =
      std::make_unique<std::string>(value);
    const tl::expected<int, const char*> with_error =
      tl::unexpected("no value");

    CHECK(*value_or_throw<TestException>(std::move(with_value)) == value);
    CHECK_THROWS_WITH(value_or_throw<TestException>(std::move(with_error)),
                      "no value");
  }

  SUBCASE("const ref with prefix")
  {
    const tl::expected<int, const char*> with_value = 42;
    const tl::expected<int, const char*> with_error =
      tl::unexpected("no value");

    CHECK(value_or_throw<TestException>(with_value, "prefix: ") == 42);
    CHECK_THROWS_WITH(value_or_throw<TestException>(with_error, "prefix: "),
                      "prefix: no value");
  }

  SUBCASE("move with prefix")
  {
    const std::string value = "value";
    tl::expected<std::unique_ptr<std::string>, const char*> with_value =
      std::make_unique<std::string>(value);
    const tl::expected<int, const char*> with_error =
      tl::unexpected("no value");

    CHECK(*value_or_throw<TestException>(std::move(with_value), "prefix: ")
          == value);
    CHECK_THROWS_WITH(
      value_or_throw<TestException>(std::move(with_error), "prefix: "),
      "prefix: no value");
  }

  SUBCASE("void T::value_type")
  {
    const tl::expected<void, const char*> without_error;
    const tl::expected<void, const char*> with_error =
      tl::unexpected("no value");

    CHECK_NOTHROW(throw_on_error<TestException>(without_error));
    CHECK_THROWS_WITH(throw_on_error<TestException>(with_error), "no value");
  }

  SUBCASE("void T::value_type with prefix")
  {
    const tl::expected<void, const char*> without_error;
    const tl::expected<void, const char*> with_error =
      tl::unexpected("no value");

    CHECK_NOTHROW(throw_on_error<TestException>(without_error, "prefix: "));
    CHECK_THROWS_WITH(throw_on_error<TestException>(with_error, "prefix: "),
                      "prefix: no value");
  }
}
