// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "../src/Util.hpp"
#include "TestUtil.hpp"
#include "catch2_tests.hpp"

#include "third_party/catch.hpp"
#include "third_party/fmt/core.h"

int
main(int argc, char** argv)
{
#ifdef _WIN32
  x_setenv("CCACHE_DETECT_SHEBANG", "1");
#endif
  x_unsetenv("GCC_COLORS"); // Don't confuse argument processing tests.

  std::string dir_before = Util::get_actual_cwd();
  std::string testdir = fmt::format("testdir.{}", getpid());
  Util::wipe_path(testdir);
  Util::create_dir(testdir);
  TestUtil::check_chdir(testdir);

  int result = run_catch2_tests(argc, argv);

  if (result == 0) {
    TestUtil::check_chdir(dir_before);
    Util::wipe_path(testdir);
  } else {
    fmt::print(stderr, "Note: Test data has been left in {}\n", testdir);
  }

  return result;
}
