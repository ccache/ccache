// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <util/path_utils.hpp>

#include <third_party/doctest.h>

TEST_CASE("util::is_absolute_path")
{
#ifdef _WIN32
  CHECK(util::is_absolute_path("C:/"));
  CHECK(util::is_absolute_path("C:\\foo/fie"));
  CHECK(util::is_absolute_path("/C:\\foo/fie")); // MSYS/Cygwin path
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("foo\\fie/fum"));
  CHECK(!util::is_absolute_path("C:foo/fie"));
#endif
  CHECK(util::is_absolute_path("/"));
  CHECK(util::is_absolute_path("/foo/fie"));
  CHECK(!util::is_absolute_path(""));
  CHECK(!util::is_absolute_path("foo/fie"));
}
