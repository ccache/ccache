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

#include "Counters.hpp"

#include "stats.hpp"

#include <algorithm>

Counters::Counters() : m_counters(static_cast<size_t>(Statistic::END))
{
}

uint64_t
Counters::get(Statistic statistic) const
{
  assert(static_cast<size_t>(statistic) < static_cast<size_t>(Statistic::END));
  const size_t i = static_cast<size_t>(statistic);
  return i < m_counters.size() ? m_counters[i] : 0;
}

void
Counters::set(Statistic statistic, uint64_t value)
{
  const auto i = static_cast<size_t>(statistic);
  if (i >= m_counters.size()) {
    m_counters.resize(i + 1);
  }
  m_counters[i] = value;
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
