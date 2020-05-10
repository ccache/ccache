// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

#include <unistd.h>

using Catch::Equals;
using TestUtil::TestContext;

TEST_CASE("Default constructor")
{
  Stat stat;
  CHECK(!stat);
  CHECK(stat.error_number() == -1);
  CHECK(stat.device() == 0);
  CHECK(stat.inode() == 0);
  CHECK(stat.mode() == 0);
  CHECK(stat.ctime() == 0);
  CHECK(stat.mtime() == 0);
  CHECK(stat.size() == 0);
  CHECK(stat.size_on_disk() == 0);
  CHECK(!stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());
}

TEST_CASE("Named constructors")
{
  CHECK(!Stat::stat("does_not_exist"));
  CHECK(!Stat::stat("does_not_exist", Stat::OnError::ignore));
  CHECK(!Stat::stat("does_not_exist", Stat::OnError::log));
  CHECK_THROWS_WITH(
    Stat::stat("does_not_exist", Stat::OnError::throw_error),
    Equals("failed to stat does_not_exist: No such file or directory"));
}

TEST_CASE("Same i-node as")
{
  TestContext test_context;

  Util::write_file("a", "");
  Util::write_file("b", "");
  auto a_stat = Stat::stat("a");
  auto b_stat = Stat::stat("b");

  CHECK(a_stat.same_inode_as(a_stat));
#ifdef _WIN32 // no i-node concept
  (void)b_stat;
#else
  CHECK(!a_stat.same_inode_as(b_stat));
#endif

  Util::write_file("a", "change size");
  auto new_a_stat = Stat::stat("a");
  CHECK(new_a_stat.same_inode_as(a_stat));
}

TEST_CASE("Return values when file is missing")
{
  auto stat = Stat::stat("does_not_exist");
  CHECK(!stat);
  CHECK(stat.error_number() == ENOENT);
  CHECK(stat.device() == 0);
  CHECK(stat.inode() == 0);
  CHECK(stat.mode() == 0);
  CHECK(stat.ctime() == 0);
  CHECK(stat.mtime() == 0);
  CHECK(stat.size() == 0);
  CHECK(stat.size_on_disk() == 0);
  CHECK(!stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());
}

TEST_CASE("Return values when file exists")
{
  TestContext test_context;

  Util::write_file("file", "1234567");

  auto stat = Stat::stat("file");
  struct stat st;
  CHECK(::stat("file", &st) == 0);

  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(stat.device() == st.st_dev);
  CHECK(stat.inode() == st.st_ino);
  CHECK(stat.mode() == st.st_mode);
  CHECK(stat.ctime() == st.st_ctime);
  CHECK(stat.mtime() == st.st_mtime);
  CHECK(stat.size() == st.st_size);
#ifdef _WIN32
  CHECK(stat.size_on_disk() == ((stat.size() + 1023) & ~1023));
#else
  CHECK(stat.size_on_disk() == st.st_blocks * 512);
#endif
  CHECK(!stat.is_directory());
  CHECK(stat.is_regular());
  CHECK(!stat.is_symlink());
}

TEST_CASE("Directory")
{
  TestContext test_context;

  REQUIRE(mkdir("directory", 0456) == 0);
  auto stat = Stat::stat("directory");

  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());
}

#ifndef _WIN32
TEST_CASE("Symlinks")
{
  TestContext test_context;

  Util::write_file("file", "1234567");

  SECTION("file lstat")
  {
    auto stat = Stat::lstat("file", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(stat.size() == 7);
  }

  SECTION("file stat")
  {
    auto stat = Stat::stat("file", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(stat.size() == 7);
  }

  SECTION("symlink lstat")
  {
    REQUIRE(symlink("file", "symlink") == 0);
    auto stat = Stat::lstat("symlink", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(stat.is_symlink());
    CHECK(stat.size() == 4);
  }

  SECTION("symlink stat")
  {
    REQUIRE(symlink("file", "symlink") == 0);
    auto stat = Stat::stat("symlink", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(stat.size() == 7);
  }
}
#endif
