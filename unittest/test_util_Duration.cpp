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

#include <util/Duration.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("util::Duration");

using util::Duration;

TEST_CASE("Basics")
{
  Duration d0(4711, 2042);

  CHECK(d0.sec() == 4711);
  CHECK(d0.nsec() == 4711000002042);
  CHECK(d0.nsec_decimal_part() == 2042);
}

TEST_CASE("Comparison operators")
{
  Duration d0(1000, 0);
  Duration d1(1000, 42);
  Duration d2(1001, 0);

  SUBCASE("operator==")
  {
    CHECK(d0 == d0);
    CHECK(!(d0 == d1));
    CHECK(!(d1 == d0));
    CHECK(!(d0 == d2));
    CHECK(!(d2 == d0));
  }

  SUBCASE("operator!=")
  {
    CHECK(!(d0 != d0));
    CHECK(d0 != d1);
    CHECK(d1 != d0);
  }

  SUBCASE("operator<")
  {
    CHECK(d0 < d1);
    CHECK(d0 < d2);
    CHECK(d1 < d2);
    CHECK(!(d1 < d0));
    CHECK(!(d2 < d0));
    CHECK(!(d2 < d1));
  }

  SUBCASE("operator>")
  {
    CHECK(d2 > d1);
    CHECK(d2 > d0);
    CHECK(d1 > d0);
    CHECK(!(d1 > d2));
    CHECK(!(d0 > d2));
    CHECK(!(d0 > d1));
  }

  SUBCASE("operator<=")
  {
    CHECK(d0 <= d0);
    CHECK(d0 <= d1);
    CHECK(d0 <= d2);
    CHECK(!(d1 <= d0));
    CHECK(!(d2 <= d0));
  }

  SUBCASE("operator>=")
  {
    CHECK(d2 >= d1);
    CHECK(d2 >= d0);
    CHECK(d1 >= d0);
    CHECK(!(d1 >= d2));
    CHECK(!(d0 >= d2));
  }
}

TEST_CASE("Arithmetic operators")
{
  Duration d0(1, 2);
  Duration d1(3, 9);

  SUBCASE("operator+")
  {
    Duration d = d0 + d1;
    CHECK(d.sec() == 4);
    CHECK(d.nsec_decimal_part() == 11);
  }

  SUBCASE("operator-")
  {
    Duration d = d0 - d1;
    CHECK(d.sec() == -2);
    CHECK(d.nsec_decimal_part() == -7);
  }

  SUBCASE("operator*")
  {
    Duration d = d1 * 4;
    CHECK(d.sec() == 12);
    CHECK(d.nsec_decimal_part() == 36);
  }

  SUBCASE("operator/")
  {
    Duration d = d1 / 0.8;
    CHECK(d.sec() == 3);
    CHECK(d.nsec_decimal_part() == 750'000'011);
  }
}

TEST_SUITE_END();
