// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/util/bitset.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("BitSet");

TEST_CASE("Operations")
{
  enum class Test { A = 1U << 0, B = 1U << 1, C = 1U << 2 };

  util::BitSet<Test> es;
  CHECK(es.empty());
  CHECK(!es.contains(Test::A));
  CHECK(!es.contains(Test::B));
  CHECK(es.to_bitmask() == 0);

  es.insert(Test::A);
  CHECK(!es.empty());
  CHECK(es.contains(Test::A));
  CHECK(!es.contains(Test::B));
  CHECK(es.to_bitmask() == 1);

  es.insert(Test::B);
  CHECK(!es.empty());
  CHECK(es.contains(Test::A));
  CHECK(es.contains(Test::B));
  CHECK(es.to_bitmask() == 3);

  util::BitSet<Test> es2(es);
  CHECK(!es2.empty());
  CHECK(es2.contains(Test::A));
  CHECK(es2.contains(Test::B));
  CHECK(es2.to_bitmask() == 3);

  es.erase(Test::A);
  CHECK(!es.empty());
  CHECK(!es.contains(Test::A));
  CHECK(es.contains(Test::B));
  CHECK(es.to_bitmask() == 2);

  util::BitSet<Test> es3(Test::C);
  es3.insert(es2);
  CHECK(!es3.empty());
  CHECK(es3.contains(Test::A));
  CHECK(es3.contains(Test::B));
  CHECK(es3.contains(Test::C));
  CHECK(es3.to_bitmask() == 7);

  es3.erase(Test::B);
  CHECK(!es3.empty());
  CHECK(es3.contains(Test::A));
  CHECK(!es3.contains(Test::B));
  CHECK(es3.contains(Test::C));
  CHECK(es3.to_bitmask() == 5);
}

TEST_SUITE_END();
