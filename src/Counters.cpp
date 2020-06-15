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

Counters::Counters() : m_counters(STATS_END)
{
}

// clang-format off
unsigned&
Counters::operator[](size_t index)
// clang-format on
{
  if (index >= m_counters.size()) {
    m_counters.resize(index + 1);
  }
  return m_counters.at(index);
}

// clang-format off
unsigned
Counters::operator[](size_t index) const
// clang-format on
{
  return index < m_counters.size() ? m_counters.at(index) : 0;
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
