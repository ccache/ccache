// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "../src/Lockfile.hpp"
#include "../src/Stat.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

using TestUtil::TestContext;

TEST_CASE("Lockfile acquire and release")
{
  TestContext test_context;

  {
    Lockfile lock("test", 1000);
    CHECK(lock.acquired());
    auto st = Stat::lstat("test.lock");
    CHECK(st);
#ifndef _WIN32
    CHECK(st.is_symlink());
#else
    CHECK(st.is_regular());
#endif
  }

  CHECK(!Stat::lstat("test.lock"));
}

#ifndef _WIN32
TEST_CASE("Lockfile breaking")
{
  TestContext test_context;

  CHECK(symlink("foo", "test.lock") == 0);

  Lockfile lock("test", 1000);
  CHECK(lock.acquired());
}
#endif // !_WIN32
