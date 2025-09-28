// Copyright (C) 2010-2024 Joel Rosdahl and other contributors
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

#include "statisticscounters.hpp"

#include <ccache/util/assertions.hpp>

#include <algorithm>

namespace core {

StatisticsCounters::StatisticsCounters()
  : m_counters(static_cast<size_t>(Statistic::END))
{
}

StatisticsCounters::StatisticsCounters(const Statistic statistic)
  : StatisticsCounters({statistic})
{
}

StatisticsCounters::StatisticsCounters(
  const std::initializer_list<Statistic> statistics)
  : StatisticsCounters()
{
  for (auto st : statistics) {
    increment(st);
  }
}

uint64_t
StatisticsCounters::get(Statistic statistic) const
{
  const auto index = static_cast<size_t>(statistic);
  ASSERT(index < static_cast<size_t>(Statistic::END));
  return index < m_counters.size() ? m_counters[index] : 0;
}

uint64_t
StatisticsCounters::get_offsetted(Statistic statistic, size_t offset) const
{
  return get_raw(static_cast<size_t>(statistic) + offset);
}

uint64_t
StatisticsCounters::get_raw(size_t index) const
{
  ASSERT(index < size());
  return m_counters[index];
}

void
StatisticsCounters::set(Statistic statistic, uint64_t value)
{
  const auto index = static_cast<size_t>(statistic);
  ASSERT(index < static_cast<size_t>(Statistic::END));
  m_counters[index] = value;
}

void
StatisticsCounters::set_offsetted(Statistic statistic,
                                  size_t offset,
                                  uint64_t value)
{
  set_raw(static_cast<size_t>(statistic) + offset, value);
}

void
StatisticsCounters::set_raw(size_t index, uint64_t value)
{
  if (index >= m_counters.size()) {
    m_counters.resize(index + 1);
  }
  m_counters[index] = value;
}

void
StatisticsCounters::increment(Statistic statistic, int64_t value)
{
  if (value == 0) {
    return;
  }
  const auto i = static_cast<size_t>(statistic);
  if (i >= m_counters.size()) {
    m_counters.resize(i + 1);
  }
  auto& counter = m_counters[i];
  counter =
    std::max(static_cast<int64_t>(0), static_cast<int64_t>(counter + value));
}

void
StatisticsCounters::increment(const StatisticsCounters& other)
{
  m_counters.resize(std::max(size(), other.size()));
  for (size_t i = 0; i < other.size(); ++i) {
    auto& counter = m_counters[i];
    counter = std::max(static_cast<int64_t>(0),
                       static_cast<int64_t>(counter + other.m_counters[i]));
  }
}

void
StatisticsCounters::increment_offsetted(Statistic statistic,
                                        size_t offset,
                                        int64_t value)
{
  increment(static_cast<Statistic>(static_cast<size_t>(statistic) + offset),
            value);
}

size_t
StatisticsCounters::size() const
{
  return m_counters.size();
}

bool
StatisticsCounters::all_zero() const
{
  return !std::any_of(
    m_counters.begin(), m_counters.end(), [](unsigned v) { return v != 0; });
}

} // namespace core
