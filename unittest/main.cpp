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
#include "../src/legacy_util.hpp"
#include "catch2_tests.hpp"
#include "framework.hpp"

unsigned suite_hashutil(unsigned);
unsigned suite_legacy_util(unsigned);

const suite_fn k_legacy_suites[] = {
  &suite_hashutil,
  &suite_legacy_util,
  nullptr,
};

int
main(int argc, char** argv)
{
#ifdef _WIN32
  x_setenv("CCACHE_DETECT_SHEBANG", "1");
#endif

  std::string dir_before = Util::get_actual_cwd();
  char* testdir = format("testdir.%d", (int)getpid());
  cct_create_fresh_dir(testdir);
  cct_chdir(testdir);

  // Run Catch2 tests.
  int result = run_catch2_tests(argc, argv);

  // Run legacy tests.
  if (result == 0) {
    bool verbose = false;
    result = cct_run(k_legacy_suites, verbose);
  }

  if (result == 0) {
    cct_chdir(dir_before.c_str());
    cct_wipe(testdir);
  }
  free(testdir);
  return result;
}
