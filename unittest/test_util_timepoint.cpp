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

#include <ccache/util/timepoint.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("util::TimePoint");

using util::TimePoint;

TEST_CASE("Basics")
{
  TimePoint t0(4711, 2042);

  CHECK(t0.sec() == 4711);
  CHECK(t0.nsec() == 4711000002042);
  CHECK(t0.nsec_decimal_part() == 2042);
}

TEST_CASE("Conversions")
{
  TimePoint t0(4711, 2042);

  SUBCASE("to_timespec")
  {
    timespec ts = t0.to_timespec();
    CHECK(ts.tv_sec == 4711);
    CHECK(ts.tv_nsec == 2042);
  }
}

TEST_CASE("Comparison operators")
{
  TimePoint t0(1000, 0);
  TimePoint t1(1000, 42);
  TimePoint t2(1001, 0);

  SUBCASE("operator==")
  {
    CHECK(t0 == t0);
    CHECK(!(t0 == t1));
    CHECK(!(t1 == t0));
    CHECK(!(t0 == t2));
    CHECK(!(t2 == t0));
  }

  SUBCASE("operator!=")
  {
    CHECK(!(t0 != t0));
    CHECK(t0 != t1);
    CHECK(t1 != t0);
  }

  SUBCASE("operator<")
  {
    CHECK(t0 < t1);
    CHECK(t0 < t2);
    CHECK(t1 < t2);
    CHECK(!(t1 < t0));
    CHECK(!(t2 < t0));
    CHECK(!(t2 < t1));
  }

  SUBCASE("operator>")
  {
    CHECK(t2 > t1);
    CHECK(t2 > t0);
    CHECK(t1 > t0);
    CHECK(!(t1 > t2));
    CHECK(!(t0 > t2));
    CHECK(!(t0 > t1));
  }

  SUBCASE("operator<=")
  {
    CHECK(t0 <= t0);
    CHECK(t0 <= t1);
    CHECK(t0 <= t2);
    CHECK(!(t1 <= t0));
    CHECK(!(t2 <= t0));
  }

  SUBCASE("operator>=")
  {
    CHECK(t2 >= t1);
    CHECK(t2 >= t0);
    CHECK(t1 >= t0);
    CHECK(!(t1 >= t2));
    CHECK(!(t0 >= t2));
  }
}

TEST_CASE("Operations with duration")
{
  TimePoint t0(1, 2);
  TimePoint t1(3, 17);

  SUBCASE("operator-(TimePoint)")
  {
    CHECK(t1 - t0 == util::Duration(2, 15));
    CHECK(t0 - t1 == util::Duration(-2, -15));
  }

  SUBCASE("operator+(Duration)")
  {
    CHECK(t0 + util::Duration(4, 999999999) == util::TimePoint(6, 1));
  }

  SUBCASE("operator-(Duration))")
  {
    auto t = t0 - util::Duration(4, 999999999);
    CHECK(t.sec() == -3);
    CHECK(t.nsec_decimal_part() == -999999997);
  }
}

TEST_SUITE_END();
