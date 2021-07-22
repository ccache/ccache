// Copyright (C) 2011-2021 Joel Rosdahl and other contributors
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

#include <core/Statistic.hpp>
#include <core/Statistics.hpp>

#include <third_party/doctest.h>

#include <iostream> // macOS bug: https://github.com/onqtam/doctest/issues/126

using core::Statistic;
using core::Statistics;
using core::StatisticsCounters;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("core::Statistics");

TEST_CASE("get_result_id")
{
  TestContext test_context;

  StatisticsCounters counters;
  counters.increment(Statistic::cache_miss);

  CHECK(*Statistics(counters).get_result_id() == "cache_miss");
}

TEST_CASE("get_result_message")
{
  TestContext test_context;

  StatisticsCounters counters;
  counters.increment(Statistic::cache_miss);

  CHECK(*Statistics(counters).get_result_message() == "cache miss");
}

TEST_SUITE_END();
