// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/cxx_modules.hpp>

#include <doctest/doctest.h>
#include <tl/expected.hpp>

#include <string_view>

using namespace std::literals;
using namespace cxx_modules::depfiles;

using TestUtil::TestContext;

TEST_SUITE_BEGIN("cxx_modules");

TEST_CASE("DepFile::parse")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "revision": 0,
    "rules": [
      {
        "work-directory": "src",
        "primary-output": "foo.o",
        "provides": [
          {
            "logical-name": "foo"
          }
        ],
        "requires": [
          {
            "logical-name": "bar",
            "lookup-method": "by-name"
          }
        ]
      }
    ]
  })"sv;
  const auto dep_file = DepFile::parse(buffer);

  CHECK(dep_file.has_value());
  CHECK(dep_file->version == 1);
  CHECK(dep_file->revision == 0);
  CHECK(dep_file->rules.size() == 1);
  CHECK(dep_file->rules[0].work_directory == "src"sv);
  CHECK(dep_file->rules[0].primary_output == "foo.o"sv);

  CHECK(dep_file->rules[0].provides.has_value());
  CHECK(dep_file->rules[0].provides->size() == 1);
  CHECK(dep_file->rules[0].provides->operator[](0).logical_name == "foo"sv);

  CHECK(dep_file->rules[0].requires_.has_value());
  CHECK(dep_file->rules[0].requires_->size() == 1);
  CHECK(dep_file->rules[0].requires_->operator[](0).logical_name == "bar"sv);
  CHECK(dep_file->rules[0].requires_->operator[](0).lookup_method
        == LookupMethod::ByName);
}

TEST_CASE("DepFile::parse error expected_bracket")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "revision": 0,
    "rules":
  })"sv;
  const auto dep_file = DepFile::parse(buffer);

  CHECK(!dep_file.has_value());
  CHECK(dep_file.error() == DepFile::ParseError::code::expected_bracket);
  CHECK(dep_file.error().format(buffer).contains("expected_bracket"));
}

TEST_CASE("DepFile::parse error missing_key")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "revision": 0
  })"sv;
  const auto dep_file = DepFile::parse(buffer);

  CHECK(!dep_file.has_value());
  CHECK(dep_file.error() == DepFile::ParseError::code::missing_key);
  CHECK(dep_file.error().format(buffer).contains("missing_key"));
}

TEST_SUITE_END();
