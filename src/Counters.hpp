// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

#include "system.hpp"

#include <vector>

// A simple wrapper around a vector of integers
// used for the statistics counters.
class Counters
{
public:
  Counters();

  unsigned& operator[](size_t index);
  unsigned operator[](size_t index) const;

  size_t size() const;

  // Return true if all counters are zero, false otherwise.
  bool all_zero() const;

private:
  std::vector<unsigned> m_counters;
};
