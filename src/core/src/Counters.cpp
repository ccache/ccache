// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "core/Counters.hpp"

#include "core/Statistics.hpp"
#include "core/assertions.hpp"

#include <algorithm>

Counters::Counters() : m_counters(static_cast<size_t>(Statistic::END))
{
}

uint64_t
Counters::get(Statistic statistic) const
{
  const auto index = static_cast<size_t>(statistic);
  ASSERT(index < static_cast<size_t>(Statistic::END));
  return index < m_counters.size() ? m_counters[index] : 0;
}

void
Counters::set(Statistic statistic, uint64_t value)
{
  const auto index = static_cast<size_t>(statistic);
  ASSERT(index < static_cast<size_t>(Statistic::END));
  m_counters[index] = value;
}

uint64_t
Counters::get_raw(size_t index) const
{
  ASSERT(index < size());
  return m_counters[index];
}

void
Counters::set_raw(size_t index, uint64_t value)
{
  if (index >= m_counters.size()) {
    m_counters.resize(index + 1);
  }
  m_counters[index] = value;
}

void
Counters::increment(Statistic statistic, int64_t value)
{
  const auto i = static_cast<size_t>(statistic);
  if (i >= m_counters.size()) {
    m_counters.resize(i + 1);
  }
  auto& counter = m_counters[i];
  counter =
    std::max(static_cast<int64_t>(0), static_cast<int64_t>(counter + value));
}

void
Counters::increment(const Counters& other)
{
  m_counters.resize(std::max(size(), other.size()));
  for (size_t i = 0; i < other.size(); ++i) {
    auto& counter = m_counters[i];
    counter = std::max(static_cast<int64_t>(0),
                       static_cast<int64_t>(counter + other.m_counters[i]));
  }
}

size_t
Counters::size() const
{
  return m_counters.size();
}

bool
Counters::all_zero() const
{
  return !std::any_of(
    m_counters.begin(), m_counters.end(), [](unsigned v) { return v != 0; });
}
