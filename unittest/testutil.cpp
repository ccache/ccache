// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include <ccache/core/exceptions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>

namespace fs = util::filesystem;

namespace TestUtil {

size_t TestContext::m_subdir_counter = 0;

TestContext::TestContext()
  : m_test_dir(util::value_or_throw<core::Error>(
      fs::current_path(), "Failed to retrieve current directory"))
{
  if (m_test_dir.parent_path().filename() != "testdir") {
    throw core::Error("TestContext instantiated outside test directory");
  }
  ++m_subdir_counter;
  fs::path subtest_dir = m_test_dir / FMT("test_{}", m_subdir_counter);
  util::throw_on_error<core::Error>(fs::create_directories(subtest_dir),
                                    FMT("Failed to create {}: ", subtest_dir));
  util::throw_on_error<core::Error>(
    fs::current_path(subtest_dir),
    FMT("Failed to change directory to {}", subtest_dir));
}

TestContext::~TestContext()
{
  std::ignore = fs::current_path(m_test_dir);
}

} // namespace TestUtil
