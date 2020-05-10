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

#pragma once

#include "../src/Util.hpp"

namespace TestUtil {

// This class is intended to be instantiated in all test cases that create local
// files.
class TestContext
{
public:
  TestContext();
  ~TestContext();

private:
  std::string m_test_dir;
  static size_t m_subdir_counter;
};

// Change directory to `dir`, throwing Error on failure.
void check_chdir(const std::string& dir);

} // namespace TestUtil
