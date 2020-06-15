// Copyright (C) 2011-2019 Joel Rosdahl and other contributors
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

#include "../src/AtomicFile.hpp"
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

using Catch::Equals;
using TestUtil::TestContext;

TEST_CASE("Base case")
{
  TestContext test_context;

  AtomicFile atomic_file("test", AtomicFile::Mode::text);
  atomic_file.write("h");
  atomic_file.write(std::vector<uint8_t>{0x65, 0x6c});
  fputs("lo", atomic_file.stream());
  atomic_file.commit();
  CHECK(Util::read_file("test") == "hello");
}

TEST_CASE("Not committing")
{
  TestContext test_context;

  {
    AtomicFile atomic_file("test", AtomicFile::Mode::text);
    atomic_file.write("hello");
  }
  CHECK_THROWS_WITH(Util::read_file("test"),
                    Equals("No such file or directory"));
}
