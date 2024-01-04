// Copyright (C) 2011-2022 Joel Rosdahl and other contributors
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

#include "TestUtil.hpp"

#include <Util.hpp>
#include <core/Statistic.hpp>
#include <storage/local/StatsFile.hpp>
#include <util/file.hpp>
#include <util/fmtmacros.hpp>

#include <third_party/doctest.h>

using core::Statistic;
using storage::local::StatsFile;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("storage::local::StatsFile");

TEST_CASE("Read nonexistent")
{
  TestContext test_context;

  const auto counters = StatsFile("test").read();

  REQUIRE(counters.size() == static_cast<size_t>(Statistic::END));
  CHECK(counters.get(Statistic::cache_miss) == 0);
}

TEST_CASE("Read bad")
{
  TestContext test_context;

  util::write_file("test", "bad 1 2 3 4 5\n");
  const auto counters = StatsFile("test").read();

  REQUIRE(counters.size() == static_cast<size_t>(Statistic::END));
  CHECK(counters.get(Statistic::cache_miss) == 0);
}

TEST_CASE("Read existing")
{
  TestContext test_context;

  util::write_file("test", "0 1 2 3 27 5\n");
  const auto counters = StatsFile("test").read();

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
    content += FMT("{}\n", i);
  }

  util::write_file("test", content);
  const auto counters = StatsFile("test").read();

  REQUIRE(counters.size() == count);
  for (size_t i = 0; i < count; ++i) {
    CHECK(counters.get_raw(i) == i);
  }
}

TEST_CASE("Update")
{
  TestContext test_context;

  util::write_file("test", "0 1 2 3 27 5\n");

  auto counters = StatsFile("test").update([](auto& cs) {
    cs.increment(Statistic::internal_error, 1);
    cs.increment(Statistic::cache_miss, 6);
  });
  REQUIRE(counters);

  CHECK(counters->get(Statistic::internal_error) == 4);
  CHECK(counters->get(Statistic::cache_miss) == 33);

  counters = StatsFile("test").read();
  CHECK(counters->get(Statistic::internal_error) == 4);
  CHECK(counters->get(Statistic::cache_miss) == 33);
}

TEST_SUITE_END();
