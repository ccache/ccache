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

#include "../src/Win32Util.hpp"
#include "TestUtil.hpp"

#include "third_party/doctest.h"

TEST_SUITE_BEGIN("Win32Util");

TEST_CASE("Win32Util::argv_to_string")
{
  {
    const char* const argv[] = {"a", nullptr};
    CHECK(Win32Util::argv_to_string(argv, "") == R"("a")");
  }
  {
    const char* const argv[] = {"a", nullptr};
    CHECK(Win32Util::argv_to_string(argv, "p") == R"("p" "a")");
  }
  {
    const char* const argv[] = {"a", "b c", "\"d\"", "'e'", "\\\"h", nullptr};
    CHECK(Win32Util::argv_to_string(argv, "")
          == R"("a" "b c" "\"d\"" "'e'" "\\\"h")");
  }
}

TEST_SUITE_END();
