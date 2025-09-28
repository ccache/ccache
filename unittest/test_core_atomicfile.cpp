// Copyright (C) 2011-2024 Joel Rosdahl and other contributors
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

#include <ccache/core/atomicfile.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/file.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using core::AtomicFile;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("core::AtomicFile");

TEST_CASE("Base case")
{
  TestContext test_context;

  AtomicFile atomic_file("test", AtomicFile::Mode::text);
  atomic_file.write("h");
  atomic_file.write(std::vector<uint8_t>{0x65, 0x6c});
  fputs("lo", atomic_file.stream());
  atomic_file.commit();
  CHECK(*util::read_file<std::string>("test") == "hello");
}

TEST_CASE("Not committing")
{
  TestContext test_context;

  {
    AtomicFile atomic_file("test", AtomicFile::Mode::text);
    atomic_file.write("hello");
  }
  CHECK(!util::DirEntry("test"));
}

TEST_SUITE_END();
