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

#include <ccache/cxx_modules/json.hpp>
#include <ccache/cxx_modules/p1689.hpp>

#include <doctest/doctest.h>
#include <tl/expected.hpp>

#include <string_view>

using namespace std::literals;
using namespace cxx_modules;

using TestUtil::TestContext;

TEST_SUITE_BEGIN("cxx_modules");

TEST_CASE("json::parse<deps::p1689::DepFile>")
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
  const auto deps = json::parse<deps::p1689::DepFile>(buffer);

  CHECK(deps.has_value());
  CHECK(deps->version == 1);
  CHECK(deps->revision == 0);
  CHECK(deps->rules.size() == 1);
  CHECK(deps->rules[0].work_directory == "src"sv);
  CHECK(deps->rules[0].primary_output == "foo.o"sv);

  CHECK(deps->rules[0].provides.has_value());
  CHECK(deps->rules[0].provides->size() == 1);
  CHECK(deps->rules[0].provides->operator[](0).logical_name == "foo"sv);

  CHECK(deps->rules[0].requires_.has_value());
  CHECK(deps->rules[0].requires_->size() == 1);
  CHECK(deps->rules[0].requires_->operator[](0).logical_name == "bar"sv);
  CHECK(deps->rules[0].requires_->operator[](0).lookup_method
        == deps::p1689::LookupMethod::ByName);
}

TEST_CASE("json::parse<deps::p1689::DepFile>: ignores unknown keys")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "UNKNOWN": {},
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
  const auto deps = json::parse<deps::p1689::DepFile>(buffer);

  CHECK(deps.has_value());
}

TEST_CASE("json::parse<deps::p1689::DepFile>: performs partial reads")
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

  deps::p1689::DepFile deps{};
  CHECK(deps.rules.size() == 0);

  // Override default parse options to perform a short-circuiting partial read.
  static constexpr auto opts = [] {
    auto val = json::ParseOpts;
    val.partial_read = true;
    return val;
  }();

  // Perform a partial read

  // NOTE: Since `deps.rules.size() == 0`, partial read will skip "rules".
  CHECK(json::parse<deps::p1689::DepFile, opts>(buffer, deps));

  CHECK(deps.version == 1);
  CHECK(deps.rules.size() == 0);

  // Resize `deps.rules` to 1, allocating a default value.
  deps.rules.resize(1);
  CHECK(not deps.rules[0].work_directory.has_value());
  CHECK(not deps.rules[0].primary_output.has_value());

  // Perform another partial read.

  // NOTE: Since `deps.rules.size() == 1`, the partial read will continue
  // parsing and update the default value from the contents of the buffer.
  const auto result = json::parse<deps::p1689::DepFile, opts>(buffer, deps);

  CHECK(deps.version == 1);
  CHECK(deps.rules.size() == 1);
  CHECK(deps.rules[0].work_directory == "src"sv);
  CHECK(deps.rules[0].primary_output == "foo.o"sv);

  // However, a parse error is also returned indicating where the parser
  // expected to short-circuit (e.g., not descending into "rules"). The error is
  // safe to ignore in this context.
  CHECK(not result.has_value());
  CHECK(result.error().format(buffer).contains("expected_comma"));
}

TEST_CASE("json::parse<deps::p1689::DepFile>: error expected_bracket")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "revision": 0,
    "rules":
  })"sv;
  const auto deps = json::parse<deps::p1689::DepFile>(buffer);

  CHECK(not deps.has_value());
  CHECK(deps.error() == json::ParseError::code::expected_bracket);
  CHECK(deps.error().format(buffer).contains("expected_bracket"));
}

TEST_CASE("json::parse<deps::p1689::DepFile>: error missing_key")
{
  static constexpr auto buffer = R"({
    "version": 1,
    "revision": 0
  })"sv;
  const auto deps = json::parse<deps::p1689::DepFile>(buffer);

  CHECK(not deps.has_value());
  CHECK(deps.error() == json::ParseError::code::missing_key);
  CHECK(deps.error().format(buffer).contains("missing_key"));
}

TEST_SUITE_END();
