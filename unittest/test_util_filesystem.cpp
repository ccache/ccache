// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>

#include <doctest/doctest.h>

namespace fs = util::filesystem;

using TestUtil::TestContext;

#ifdef _WIN32
TEST_CASE("util::filesystem::rename onto a directory")
{
  TestContext test_context;

  REQUIRE(util::write_file("src", "new"));
  REQUIRE(fs::create_directory("dest"));

  const auto result = fs::rename("src", "dest");
  REQUIRE(!result);
  CHECK(result.error() == std::errc::permission_denied);
}
#endif
