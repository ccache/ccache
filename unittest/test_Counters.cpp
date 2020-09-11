// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "../src/Counters.hpp"
#include "../src/stats.hpp"
#include "TestUtil.hpp"

#include "third_party/doctest.h"

using TestUtil::TestContext;

TEST_SUITE_BEGIN("Counters");

TEST_CASE("Counters")
{
  TestContext test_context;

  Counters counters;
  CHECK(counters.size() == static_cast<size_t>(Statistic::END));

  SUBCASE("Get and set statistic")
  {
    CHECK(counters.get(Statistic::cache_miss) == 0);
    counters.set(Statistic::cache_miss, 27);
    CHECK(counters.get(Statistic::cache_miss) == 27);
  }

  SUBCASE("Get and set raw index")
  {
    CHECK(counters.get_raw(4) == 0);
    counters.set_raw(4, 27);
    CHECK(counters.get(Statistic::cache_miss) == 27);
  }

  SUBCASE("Set future raw counter")
  {
    const auto future_index = static_cast<size_t>(Statistic::END) + 2;
    counters.set_raw(future_index, 42);
    CHECK(counters.get_raw(future_index) == 42);
  }

  SUBCASE("Increment single counter")
  {
    counters.set(Statistic::cache_miss, 4);

    counters.increment(Statistic::cache_miss);
    CHECK(counters.get(Statistic::cache_miss) == 5);

    counters.increment(Statistic::cache_miss, -3);
    CHECK(counters.get(Statistic::cache_miss) == 2);

    counters.increment(Statistic::cache_miss, -3);
    CHECK(counters.get(Statistic::cache_miss) == 0);
  }

  SUBCASE("Increment many counters")
  {
    counters.set(Statistic::direct_cache_hit, 3);
    counters.set(Statistic::cache_miss, 2);
    counters.set(Statistic::files_in_cache, 10);
    counters.set(Statistic::cache_size_kibibyte, 1);

    Counters updates;
    updates.set(Statistic::direct_cache_hit, 6);
    updates.set(Statistic::cache_miss, 5);
    updates.set(Statistic::files_in_cache, -1);
    updates.set(Statistic::cache_size_kibibyte, -4);

    counters.increment(updates);
    CHECK(counters.get(Statistic::direct_cache_hit) == 9);
    CHECK(counters.get(Statistic::cache_miss) == 7);
    CHECK(counters.get(Statistic::files_in_cache) == 9);
    CHECK(counters.get(Statistic::cache_size_kibibyte) == 0); // No wrap-around
  }
}

TEST_SUITE_END();
