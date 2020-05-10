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

#include "TestUtil.hpp"

#include "../src/exceptions.hpp"

namespace TestUtil {

size_t TestContext::m_subdir_counter = 0;

TestContext::TestContext() : m_test_dir(Util::get_actual_cwd())
{
  if (!Util::base_name(m_test_dir).starts_with("testdir.")) {
    throw Error("TestContext instantiated outside test directory");
  }
  ++m_subdir_counter;
  std::string subtest_dir =
    fmt::format("{}/test_{}", m_test_dir, m_subdir_counter);
  Util::create_dir(subtest_dir);
  if (chdir(subtest_dir.c_str()) != 0) {
    abort();
  }
}

TestContext::~TestContext()
{
  if (chdir(m_test_dir.c_str()) != 0) {
    abort();
  }
}

void
check_chdir(const std::string& dir)
{
  if (chdir(dir.c_str()) != 0) {
    throw Error(fmt::format(
      "failed to change directory to {}: {}", dir, strerror(errno)));
  }
}

} // namespace TestUtil
