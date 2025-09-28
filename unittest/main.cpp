// Copyright (C) 2010-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/environment.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>

#define DOCTEST_THREAD_LOCAL // Avoid MinGW thread_local bug
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <string>

namespace fs = util::filesystem;

tl::expected<int, std::error_code>
prepare_test(int argc, char** argv)
{
  auto dir_before = *fs::current_path();
  fs::path testdir = FMT("testdir/{}", getpid());

  TRY(fs::remove_all(testdir));
  TRY(fs::create_directories(testdir));
  TRY(fs::current_path(testdir));

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  int result = context.run();

  if (result == EXIT_SUCCESS) {
    TRY(fs::current_path(dir_before));
    TRY(fs::remove_all(testdir));
  } else {
    PRINT(stderr, "Note: Test data has been left in {}\n", testdir);
  }

  return result;
}

int
main(int argc, char** argv)
{
#ifdef _WIN32
  util::setenv("_CCACHE_TEST", "1");
#endif
  util::unsetenv("GCC_COLORS"); // Don't confuse argument processing tests.

  auto result = prepare_test(argc, argv);
  if (result) {
    return *result;
  } else {
    PRINT(stderr, "error: {}\n", result.error());
    return EXIT_FAILURE;
  }
}
