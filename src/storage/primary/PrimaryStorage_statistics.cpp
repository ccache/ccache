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

#include "PrimaryStorage.hpp"

#include <Config.hpp>
#include <core/Statistics.hpp>
#include <fmtmacros.hpp>
#include <storage/primary/StatsFile.hpp>

#include <algorithm>

namespace storage {
namespace primary {

static void
for_each_level_1_and_2_stats_file(
  const std::string& cache_dir,
  const std::function<void(const std::string& path)> function)
{
  for (size_t level_1 = 0; level_1 <= 0xF; ++level_1) {
    function(FMT("{}/{:x}/stats", cache_dir, level_1));
    for (size_t level_2 = 0; level_2 <= 0xF; ++level_2) {
      function(FMT("{}/{:x}/{:x}/stats", cache_dir, level_1, level_2));
    }
  }
}

// Zero all statistics counters except those tracking cache size and number of
// files in the cache.
void
PrimaryStorage::zero_all_statistics()
{
  const time_t timestamp = time(nullptr);
  const auto zeroable_fields = core::Statistics::get_zeroable_fields();

  for_each_level_1_and_2_stats_file(
    m_config.cache_dir(), [=](const std::string& path) {
      StatsFile(path).update([=](auto& cs) {
        for (const auto statistic : zeroable_fields) {
          cs.set(statistic, 0);
        }
        cs.set(core::Statistic::stats_zeroed_timestamp, timestamp);
      });
    });
}

// Get statistics and last time of update for the whole primary storage cache.
std::pair<core::StatisticsCounters, time_t>
PrimaryStorage::get_all_statistics() const
{
  core::StatisticsCounters counters;
  uint64_t zero_timestamp = 0;
  time_t last_updated = 0;

  // Add up the stats in each directory.
  for_each_level_1_and_2_stats_file(
    m_config.cache_dir(), [&](const auto& path) {
      counters.set(core::Statistic::stats_zeroed_timestamp, 0); // Don't add
      counters.increment(StatsFile(path).read());
      zero_timestamp = std::max(
        counters.get(core::Statistic::stats_zeroed_timestamp), zero_timestamp);
      last_updated = std::max(last_updated, Stat::stat(path).mtime());
    });

  counters.set(core::Statistic::stats_zeroed_timestamp, zero_timestamp);
  return std::make_pair(counters, last_updated);
}

} // namespace primary
} // namespace storage
