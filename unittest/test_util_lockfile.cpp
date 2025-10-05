// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/direntry.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/lockfile.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

using namespace std::chrono_literals;

using util::DirEntry;

TEST_SUITE_BEGIN("LockFile");

using TestUtil::TestContext;

TEST_CASE("Acquire and release short-lived lock file")
{
  TestContext test_context;

  util::LockFile lock("test");
  {
    CHECK(!lock.acquired());
    CHECK(!DirEntry("test.lock"));
    CHECK(!DirEntry("test.alive"));

    CHECK(lock.acquire());
    CHECK(lock.acquired());
    DirEntry entry("test.lock");
    CHECK(entry);
#ifndef _WIN32
    CHECK(DirEntry("test.alive"));
    CHECK(entry.is_symlink());
#else
    CHECK(entry.is_regular_file());
#endif
  }

  lock.release();
  lock.release();
  CHECK(!lock.acquired());
  CHECK(!DirEntry("test.lock"));
  CHECK(!DirEntry("test.alive"));
}

TEST_CASE("Acquire and release long-lived lock file")
{
  TestContext test_context;

  util::LongLivedLockFileManager lock_manager;
  util::LockFile lock("test");
  lock.make_long_lived(lock_manager);
  {
    CHECK(!lock.acquired());
    CHECK(!DirEntry("test.lock"));
    CHECK(!DirEntry("test.alive"));

    CHECK(lock.acquire());
    CHECK(lock.acquired());
#ifndef _WIN32
    CHECK(DirEntry("test.alive"));
#endif
    DirEntry entry("test.lock");
    CHECK(entry);
#ifndef _WIN32
    CHECK(entry.is_symlink());
#else
    CHECK(entry.is_regular_file());
#endif
  }

  lock.release();
  lock.release();
  CHECK(!lock.acquired());
  CHECK(!DirEntry("test.lock"));
  CHECK(!DirEntry("test.alive"));
}

TEST_CASE("LockFile creates missing directories")
{
  TestContext test_context;

  util::LongLivedLockFileManager lock_manager;
  util::LockFile lock("a/b/c/test");
  lock.make_long_lived(lock_manager);
  CHECK(lock.acquire());
  CHECK(DirEntry("a/b/c/test.lock"));
}

#ifndef _WIN32
TEST_CASE("Break stale lock, blocking")
{
  TestContext test_context;

  REQUIRE(util::write_file("test.alive", ""));
  const util::TimePoint long_time_ago(0s);
  util::set_timestamps("test.alive", long_time_ago);
  CHECK(symlink("foo", "test.lock") == 0);

  util::LongLivedLockFileManager lock_manager;
  util::LockFile lock("test");
  lock.make_long_lived(lock_manager);
  CHECK(lock.acquire());
}

TEST_CASE("Break stale lock, non-blocking")
{
  TestContext test_context;

  REQUIRE(util::write_file("test.alive", ""));
  const util::TimePoint long_time_ago(0s);
  util::set_timestamps("test.alive", long_time_ago);
  CHECK(symlink("foo", "test.lock") == 0);

  util::LongLivedLockFileManager lock_manager;
  util::LockFile lock("test");
  lock.make_long_lived(lock_manager);
  CHECK(lock.try_acquire());
  CHECK(lock.acquired());
}
#endif // !_WIN32

TEST_SUITE_END();
