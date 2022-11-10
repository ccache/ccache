// Copyright (C) 2022 Joel Rosdahl and other contributors
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

#include <cstdint>

namespace util {

class Duration
{
public:
  explicit Duration(int64_t sec = 0, int64_t nsec = 0);

  bool operator==(const Duration& other) const;
  bool operator!=(const Duration& other) const;
  bool operator<(const Duration& other) const;
  bool operator>(const Duration& other) const;
  bool operator<=(const Duration& other) const;
  bool operator>=(const Duration& other) const;

  Duration operator+(const Duration& other) const;
  Duration operator-(const Duration& other) const;
  Duration operator*(double factor) const;
  Duration operator/(double factor) const;

  Duration operator-() const;

  int64_t sec() const;
  int64_t nsec() const;
  int32_t nsec_decimal_part() const;

private:
  int64_t m_ns = 0;
};

inline Duration::Duration(int64_t sec, int64_t nsec)
  : m_ns(1'000'000'000 * sec + nsec)
{
}

inline bool
Duration::operator==(const Duration& other) const
{
  return m_ns == other.m_ns;
}

inline bool
Duration::operator!=(const Duration& other) const
{
  return m_ns != other.m_ns;
}

inline bool
Duration::operator<(const Duration& other) const
{
  return m_ns < other.m_ns;
}

inline bool
Duration::operator>(const Duration& other) const
{
  return m_ns > other.m_ns;
}

inline bool
Duration::operator<=(const Duration& other) const
{
  return m_ns <= other.m_ns;
}

inline bool
Duration::operator>=(const Duration& other) const
{
  return m_ns >= other.m_ns;
}

inline Duration
Duration::operator+(const Duration& other) const
{
  return Duration(0, m_ns + other.m_ns);
}

inline Duration
Duration::operator-(const Duration& other) const
{
  return Duration(0, m_ns - other.m_ns);
}

inline Duration
Duration::operator*(double factor) const
{
  return Duration(0, factor * m_ns);
}

inline Duration
Duration::operator/(double factor) const
{
  return Duration(0, m_ns / factor);
}

inline int64_t
Duration::sec() const
{
  return m_ns / 1'000'000'000;
}

inline int64_t
Duration::nsec() const
{
  return m_ns;
}

inline int32_t
Duration::nsec_decimal_part() const
{
  return m_ns % 1'000'000'000;
}

} // namespace util
