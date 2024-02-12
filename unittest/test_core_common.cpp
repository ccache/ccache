// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include "TestUtil.hpp"

#include <core/common.hpp>
#include <util/DirEntry.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>

#include <doctest/doctest.h>

namespace fs = util::filesystem;

using TestUtil::TestContext;
using util::DirEntry;

TEST_SUITE_BEGIN("core");

TEST_CASE("core::ensure_dir_exists")
{
  TestContext test_context;

  CHECK_NOTHROW(core::ensure_dir_exists("/"));

  CHECK_NOTHROW(core::ensure_dir_exists("create/dir"));
  CHECK(DirEntry("create/dir").is_directory());

  util::write_file("create/dir/file", "");
  CHECK_THROWS_WITH(
    core::ensure_dir_exists("create/dir/file"),
    doctest::Contains("Failed to create directory create/dir/file:"));
}

TEST_CASE("core::rewrite_stderr_to_absolute_paths")
{
  TestContext test_context;
  util::write_file("existing", "");

  std::string input =
    "a:1:2\n"
    "existing:3:4\n"
    "c:5:6\n"
    "\x1b[01m\x1b[Kexisting:\x1b[m\x1b[K: foo\n"
    "\x1b[01m\x1b[Kexisting:47:11:\x1b[m\x1b[K: foo\n"
    "In file included from \x1b[01m\x1b[Kexisting:\x1b[m\x1b[K: foo\n"
    "In file included from \x1b[01m\x1b[Kexisting:47:11:\x1b[m\x1b[K: foo\n";
  std::string expected = FMT(
    "a:1:2\n"
    "{0}:3:4\n"
    "c:5:6\n"
    "\x1b[01m\x1b[K{0}:\x1b[m\x1b[K: foo\n"
    "\x1b[01m\x1b[K{0}:47:11:\x1b[m\x1b[K: foo\n"
    "In file included from \x1b[01m\x1b[K{0}:\x1b[m\x1b[K: foo\n"
    "In file included from \x1b[01m\x1b[K{0}:47:11:\x1b[m\x1b[K: foo\n",
    *fs::canonical("existing"));
  CHECK(core::rewrite_stderr_to_absolute_paths(input) == expected);
}

TEST_CASE("core::strip_ansi_csi_seqs")
{
  const char input[] =
    "Normal,"
    " \x1B[K\x1B[1mbold\x1B[m,"
    " \x1B[31mred\x1B[m,"
    " \x1B[1;32mbold green\x1B[m.\n";

  CHECK(core::strip_ansi_csi_seqs(input) == "Normal, bold, red, bold green.\n");
}

TEST_SUITE_END();
