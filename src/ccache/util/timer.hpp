// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <chrono>
#include <string>

class Timer
{
public:
  Timer();

  double measure_s() const;
  double measure_ms() const;

private:
  std::chrono::steady_clock::time_point m_start;
};

inline Timer::Timer()
  : m_start(std::chrono::steady_clock::now())
{
}

inline double
Timer::measure_s() const
{
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now() - m_start).count();
}

inline double
Timer::measure_ms() const
{
  return measure_s() * 1000;
}
