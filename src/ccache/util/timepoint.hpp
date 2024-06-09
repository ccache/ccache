// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/duration.hpp>

#include <cstdint>
#include <ctime>

namespace util {

class TimePoint
{
public:
  explicit TimePoint(int64_t sec = 0, int64_t nsec = 0);
  TimePoint(const TimePoint& other);
  explicit TimePoint(const timespec& timespec);

  TimePoint& operator=(const TimePoint& other);

  static TimePoint now();

  timespec to_timespec() const;

  int64_t sec() const;
  int64_t nsec() const;
  int32_t nsec_decimal_part() const;

  void set_sec(int64_t sec, uint32_t nsec = 0);
  void set_nsec(int64_t nsec);

  bool operator==(const TimePoint& other) const;
  bool operator!=(const TimePoint& other) const;
  bool operator<(const TimePoint& other) const;
  bool operator>(const TimePoint& other) const;
  bool operator<=(const TimePoint& other) const;
  bool operator>=(const TimePoint& other) const;

  TimePoint operator+(const Duration& duration) const;
  TimePoint operator-(const Duration& duration) const;

  Duration operator-(const TimePoint& other) const;

private:
  int64_t m_ns = 0;
};

inline TimePoint::TimePoint(int64_t sec, int64_t nsec)
  : m_ns(1'000'000'000 * sec + nsec)
{
}

inline TimePoint::TimePoint(const TimePoint& other) : m_ns(other.m_ns)
{
}

inline TimePoint::TimePoint(const timespec& timespec)
  : TimePoint(timespec.tv_sec, timespec.tv_nsec)
{
}

inline TimePoint&
TimePoint::operator=(const TimePoint& other)
{
  m_ns = other.m_ns;
  return *this;
}

inline timespec
TimePoint::to_timespec() const
{
  return {static_cast<time_t>(sec()), nsec_decimal_part()};
}

inline int64_t
TimePoint::sec() const
{
  return m_ns / 1'000'000'000;
}

inline int64_t
TimePoint::nsec() const
{
  return m_ns;
}

inline int32_t
TimePoint::nsec_decimal_part() const
{
  return static_cast<int32_t>(m_ns % 1'000'000'000);
}

inline void
TimePoint::set_sec(int64_t sec, uint32_t nsec)
{
  m_ns = 1'000'000'000 * sec + nsec;
}

inline void
TimePoint::set_nsec(int64_t nsec)
{
  m_ns = nsec;
}

inline bool
TimePoint::operator==(const TimePoint& other) const
{
  return m_ns == other.m_ns;
}

inline bool
TimePoint::operator!=(const TimePoint& other) const
{
  return m_ns != other.m_ns;
}

inline bool
TimePoint::operator<(const TimePoint& other) const
{
  return m_ns < other.m_ns;
}

inline bool
TimePoint::operator>(const TimePoint& other) const
{
  return m_ns > other.m_ns;
}

inline bool
TimePoint::operator<=(const TimePoint& other) const
{
  return m_ns <= other.m_ns;
}

inline bool
TimePoint::operator>=(const TimePoint& other) const
{
  return m_ns >= other.m_ns;
}

inline TimePoint
TimePoint::operator+(const Duration& duration) const
{
  return TimePoint(0, nsec() + duration.nsec());
}

inline TimePoint
TimePoint::operator-(const Duration& duration) const
{
  return TimePoint(0, nsec() - duration.nsec());
}

inline Duration
TimePoint::operator-(const TimePoint& other) const
{
  return Duration(0, nsec() - other.nsec());
}

} // namespace util
