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

#include "../src/ccache.hpp"
#include "../src/counters.hpp"
#include "framework.hpp"
#include "util.hpp"

TEST_SUITE(counters)

TEST(counters_init_0_should_allocate_0)
{
  struct counters* counters = counters_init(0);

  CHECK_INT_EQ(0, counters->allocated);
  CHECK_INT_EQ(0, counters->size);

  counters_free(counters);
}

TEST(counters_init_7_should_allocate_32)
{
  int i;
  struct counters* counters = counters_init(7);

  CHECK_INT_EQ(32, counters->allocated);
  CHECK_INT_EQ(7, counters->size);
  for (i = 0; i < 7; i++) {
    CHECK_INT_EQ(0, counters->data[i]);
  }

  counters_free(counters);
}

TEST(counters_resize_50_should_allocate_96)
{
  struct counters* counters = counters_init(0);

  CHECK_INT_EQ(0, counters->allocated);
  counters_resize(counters, 50);
  CHECK_INT_EQ(50, counters->size);
  CHECK_INT_EQ(96, counters->allocated);

  counters_free(counters);
}

TEST_SUITE_END
