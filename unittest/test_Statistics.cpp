// Copyright (C) 2011-2020 Joel Rosdahl and other contributors
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

#include "../src/Statistics.hpp"
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include "third_party/doctest.h"

using TestUtil::TestContext;

TEST_SUITE_BEGIN("Statistics");

TEST_CASE("Read nonexistent")
{
  TestContext test_context;

  Counters counters = Statistics::read("test");

  REQUIRE(counters.size() == static_cast<size_t>(Statistic::END));
  CHECK(counters.get(Statistic::cache_miss) == 0);
}

TEST_CASE("Read bad")
{
  TestContext test_context;

  Util::write_file("test", "bad 1 2 3 4 5\n");
  Counters counters = Statistics::read("test");

  REQUIRE(counters.size() == static_cast<size_t>(Statistic::END));
  CHECK(counters.get(Statistic::cache_miss) == 0);
}

TEST_CASE("Read existing")
{
  TestContext test_context;

  Util::write_file("test", "0 1 2 3 27 5\n");
  Counters counters = Statistics::read("test");

  REQUIRE(counters.size() == static_cast<size_t>(Statistic::END));
  CHECK(counters.get(Statistic::cache_miss) == 27);
  CHECK(counters.get(Statistic::could_not_use_modules) == 0);
}

TEST_CASE("Read future counters")
{
  TestContext test_context;

  std::string content;
  size_t count = static_cast<size_t>(Statistic::END) + 1;
  for (size_t i = 0; i < count; ++i) {
    content += fmt::format("{}\n", i);
  }

  Util::write_file("test", content);
  Counters counters = Statistics::read("test");

  REQUIRE(counters.size() == count);
  for (size_t i = 0; i < count; ++i) {
    CHECK(counters.get_raw(i) == i);
  }
}

TEST_CASE("Write")
{
  TestContext test_context;

  Counters counters;
  size_t count = static_cast<size_t>(Statistic::END) + 1;
  for (size_t i = 0; i < count; ++i) {
    counters.set_raw(i, i);
  }

  Statistics::write("test", counters);
  counters = Statistics::read("test");

  REQUIRE(counters.size() == count);
  for (size_t i = 0; i < count; ++i) {
    CHECK(counters.get_raw(i) == i);
  }
}

TEST_SUITE_END();
