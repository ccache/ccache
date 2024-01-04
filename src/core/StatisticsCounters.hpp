// Copyright (C) 2010-2023 Joel Rosdahl and other contributors
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

#pragma once

#include "Statistic.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace core {

// A simple wrapper around a vector of integers used for the statistics
// counters.
class StatisticsCounters
{
public:
  StatisticsCounters();
  StatisticsCounters(Statistic statistic);
  StatisticsCounters(std::initializer_list<Statistic> statistics);

  uint64_t get(Statistic statistic) const;
  uint64_t get_offsetted(Statistic statistic, size_t offset) const;
  uint64_t get_raw(size_t index) const;

  void set(Statistic statistic, uint64_t value);
  void set_offsetted(Statistic statistic, size_t offset, uint64_t value);
  void set_raw(size_t index, uint64_t value);

  void increment(Statistic statistic, int64_t value = 1);
  void increment(const StatisticsCounters& other);
  void increment_offsetted(Statistic statistic, size_t offset, int64_t value);

  size_t size() const;

  // Return true if all counters are zero, false otherwise.
  bool all_zero() const;

  bool operator==(const StatisticsCounters& other) const noexcept;
  bool operator!=(const StatisticsCounters& other) const noexcept;

private:
  std::vector<uint64_t> m_counters;
};

inline bool
StatisticsCounters::operator==(const StatisticsCounters& other) const noexcept
{
  return m_counters == other.m_counters;
}

inline bool
StatisticsCounters::operator!=(const StatisticsCounters& other) const noexcept
{
  return !(*this == other);
}

} // namespace core
