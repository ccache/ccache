// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include "../src/Util.hpp"

#include <core/exceptions.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/path.hpp>
#include <util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace fs = util::filesystem;

namespace TestUtil {

size_t TestContext::m_subdir_counter = 0;

TestContext::TestContext() : m_test_dir(util::actual_cwd())
{
  if (Util::base_name(Util::dir_name(m_test_dir)) != "testdir") {
    throw core::Error("TestContext instantiated outside test directory");
  }
  ++m_subdir_counter;
  std::string subtest_dir = FMT("{}/test_{}", m_test_dir, m_subdir_counter);
  fs::create_directories(subtest_dir);
  if (!fs::current_path(subtest_dir)) {
    abort();
  }
}

TestContext::~TestContext()
{
  if (!fs::current_path(m_test_dir)) {
    abort();
  }
}

} // namespace TestUtil
