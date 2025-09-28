// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/core/statslog.hpp>
#include <ccache/util/file.hpp>

#include <doctest/doctest.h>

using core::Statistic;
using core::StatsLog;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("core::StatsFile");

TEST_CASE("read")
{
  TestContext test_context;

  REQUIRE(util::write_file("stats.log", "# comment\ndirect_cache_hit\n"));
  const auto counters = StatsLog("stats.log").read();

  CHECK(counters.get(Statistic::direct_cache_hit) == 1);
  CHECK(counters.get(Statistic::cache_miss) == 0);
}

TEST_CASE("log_result")
{
  TestContext test_context;

  StatsLog stats_log("stats.log");
  stats_log.log_result("foo.c", {"cache_miss"});
  stats_log.log_result("bar.c", {"preprocessed_cache_hit"});

  CHECK(*util::read_file<std::string>("stats.log")
        == "# foo.c\ncache_miss\n# bar.c\npreprocessed_cache_hit\n");
}

TEST_SUITE_END();
