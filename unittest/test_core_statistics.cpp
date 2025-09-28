// Copyright (C) 2011-2024 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/core/statistic.hpp>
#include <ccache/core/statistics.hpp>

#include <doctest/doctest.h>

#include <iostream> // macOS bug: https://github.com/onqtam/doctest/issues/126
#include <string>
#include <vector>

using core::Statistic;
using core::Statistics;
using core::StatisticsCounters;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("core::Statistics");

TEST_CASE("get_statistics_ids")
{
  TestContext test_context;

  StatisticsCounters counters;
  counters.increment(Statistic::cache_size_kibibyte);
  counters.increment(Statistic::cache_miss);
  counters.increment(Statistic::direct_cache_hit);
  counters.increment(Statistic::autoconf_test);

  std::vector<std::string> expected = {
    "autoconf_test", "cache_miss", "direct_cache_hit"};
  CHECK(Statistics(counters).get_statistics_ids() == expected);
}

TEST_SUITE_END();
