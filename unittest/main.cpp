// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

#include "framework.hpp"

#define CATCH_CONFIG_RUNNER
#include "third_party/catch.hpp"

#define SUITE(name) unsigned suite_##name(unsigned);
#include "suites.hpp"
#undef SUITE

int
main(int argc, char** argv)
{
  suite_fn suites[] = {
#define SUITE(name) &suite_##name,
#include "suites.hpp"
#undef SUITE
    NULL};

#ifdef _WIN32
  putenv("CCACHE_DETECT_SHEBANG=1");
#endif

  char* testdir = format("testdir.%d", (int)getpid());
  cct_create_fresh_dir(testdir);
  char* dir_before = gnu_getcwd();
  cct_chdir(testdir);

  // Run Catch2 tests.
  Catch::Session session;
  int result = session.run(argc, argv);

  // Run legacy tests.
  if (result == 0) {
    bool verbose = false;
    result = cct_run(suites, verbose);
  }

  if (result == 0) {
    cct_chdir(dir_before);
    cct_wipe(testdir);
  }
  free(testdir);
  free(dir_before);
  return result;
}
