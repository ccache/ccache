// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/core/statistic.hpp>
#include <ccache/core/statisticscounters.hpp>
#include <ccache/util/time.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Config;

namespace core {

class Statistics
{
public:
  explicit Statistics(const StatisticsCounters& counters);

  // Return machine-readable strings representing the statistics counters.
  std::vector<std::string> get_statistics_ids() const;

  // Format cache statistics in human-readable format.
  std::string format_human_readable(const Config& config,
                                    const util::TimePoint& last_updated,
                                    uint8_t verbosity,
                                    bool from_log) const;

  // Format cache statistics in machine-readable format.
  std::string format_machine_readable(const Config& config,
                                      const util::TimePoint& last_updated,
                                      StatisticsFormat format) const;

  const StatisticsCounters& counters() const;

  static std::unordered_map<std::string, Statistic> get_id_map();

  static std::vector<Statistic> get_zeroable_fields();

private:
  const StatisticsCounters m_counters;

  uint64_t count_stats(unsigned flags) const;
  std::vector<std::pair<std::string, uint64_t>> get_stats(unsigned flags,
                                                          bool all) const;
  std::vector<std::pair<std::string, uint64_t>>
  prepare_statistics_entries(const Config& config,
                             const util::TimePoint& last_updated) const;
};

// --- Inline implementations ---

inline const StatisticsCounters&
Statistics::counters() const
{
  return m_counters;
}

} // namespace core
