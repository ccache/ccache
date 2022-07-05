// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "../src/Stat.hpp"
#include "TestUtil.hpp"

#include <Util.hpp>
#include <core/wincompat.hpp>
#include <util/LockFile.hpp>
#include <util/file.hpp>

#include "third_party/doctest.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_SUITE_BEGIN("LockFile");

using TestUtil::TestContext;

TEST_CASE("Acquire and release short-lived lock file")
{
  TestContext test_context;

  util::ShortLivedLockFile lock_file("test");
  {
    CHECK(!lock_file.acquired());
    CHECK(!Stat::lstat("test.lock"));
    CHECK(!Stat::lstat("test.alive"));

    util::LockFileGuard lock(lock_file);
    CHECK(lock_file.acquired());
    CHECK(lock.acquired());
    CHECK(!Stat::lstat("test.alive"));
    const auto st = Stat::lstat("test.lock");
    CHECK(st);
#ifndef _WIN32
    CHECK(st.is_symlink());
#else
    CHECK(st.is_regular());
#endif
  }

  CHECK(!lock_file.acquired());
  CHECK(!Stat::lstat("test.lock"));
  CHECK(!Stat::lstat("test.alive"));
}

TEST_CASE("Non-blocking short-lived lock")
{
  TestContext test_context;

  util::ShortLivedLockFile lock_file_1("test");
  CHECK(!lock_file_1.acquired());

  util::ShortLivedLockFile lock_file_2("test");
  CHECK(!lock_file_2.acquired());

  CHECK(lock_file_1.try_acquire());
  CHECK(lock_file_1.acquired());

  CHECK(!lock_file_2.try_acquire());
  CHECK(lock_file_1.acquired());
  CHECK(!lock_file_2.acquired());

  lock_file_2.release();
  CHECK(lock_file_1.acquired());
  CHECK(!lock_file_2.acquired());

  lock_file_1.release();
  CHECK(!lock_file_1.acquired());
  CHECK(!lock_file_2.acquired());
}

TEST_CASE("Acquire and release long-lived lock file")
{
  TestContext test_context;

  util::LongLivedLockFile lock_file("test");
  {
    CHECK(!lock_file.acquired());
    CHECK(!Stat::lstat("test.lock"));
    CHECK(!Stat::lstat("test.alive"));

    util::LockFileGuard lock(lock_file);
    CHECK(lock_file.acquired());
    CHECK(lock.acquired());
#ifndef _WIN32
    CHECK(Stat::lstat("test.alive"));
#endif
    const auto st = Stat::lstat("test.lock");
    CHECK(st);
#ifndef _WIN32
    CHECK(st.is_symlink());
#else
    CHECK(st.is_regular());
#endif
  }

  CHECK(!lock_file.acquired());
  CHECK(!Stat::lstat("test.lock"));
  CHECK(!Stat::lstat("test.alive"));
}

TEST_CASE("LockFile creates missing directories")
{
  TestContext test_context;

  util::ShortLivedLockFile lock_file("a/b/c/test");
  util::LockFileGuard lock(lock_file);
  CHECK(lock.acquired());
  CHECK(Stat::lstat("a/b/c/test.lock"));
}

#ifndef _WIN32
TEST_CASE("Break stale lock, blocking")
{
  TestContext test_context;

  Util::write_file("test.alive", "");
  const timespec long_time_ago{0, 0};
  util::set_timestamps("test.alive", long_time_ago);
  CHECK(symlink("foo", "test.lock") == 0);

  util::LongLivedLockFile lock_file("test");
  util::LockFileGuard lock(lock_file);
  CHECK(lock.acquired());
}

TEST_CASE("Break stale lock, non-blocking")
{
  TestContext test_context;

  Util::write_file("test.alive", "");
  const timespec long_time_ago{0, 0};
  util::set_timestamps("test.alive", long_time_ago);
  CHECK(symlink("foo", "test.lock") == 0);

  util::LongLivedLockFile lock_file("test");
  util::LockFileGuard lock(lock_file, util::LockFileGuard::Mode::non_blocking);
  CHECK(lock.acquired());
}
#endif // !_WIN32

TEST_SUITE_END();
