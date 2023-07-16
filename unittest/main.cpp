// Copyright (C) 2010-2023 Joel Rosdahl and other contributors
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

#include "../src/fmtmacros.hpp"
#include "TestUtil.hpp"

#include <util/environment.hpp>
#include <util/filesystem.hpp>
#include <util/path.hpp>

#include "third_party/fmt/core.h"

#define DOCTEST_THREAD_LOCAL // Avoid MinGW thread_local bug
#define DOCTEST_CONFIG_IMPLEMENT
#include "third_party/doctest.h"

namespace fs = util::filesystem;

int
main(int argc, char** argv)
{
#ifdef _WIN32
  util::setenv("CCACHE_DETECT_SHEBANG", "1");
#endif
  util::unsetenv("GCC_COLORS"); // Don't confuse argument processing tests.

  std::string dir_before = util::actual_cwd();
  std::string testdir = FMT("testdir/{}", getpid());
  fs::remove_all(testdir);
  fs::create_directories(testdir);
  TestUtil::check_chdir(testdir);

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  int result = context.run();

  if (result == 0) {
    TestUtil::check_chdir(dir_before);
    fs::remove_all(testdir);
  } else {
    PRINT(stderr, "Note: Test data has been left in {}\n", testdir);
  }

  return result;
}
