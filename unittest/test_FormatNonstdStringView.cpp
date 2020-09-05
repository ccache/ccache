// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "FormatNonstdStringView.hpp"

#include "third_party/doctest.h"

using nonstd::string_view;

TEST_SUITE_BEGIN("FormatNonstdStringView");

TEST_CASE("fmt::format and nonstd::string_view")
{
  string_view null;
  CHECK(fmt::format("{}", null) == "");

  const std::string s = "0123456789";

  string_view empty(s.data(), 0);
  CHECK(fmt::format("{}", empty) == "");

  string_view empty_end(s.data() + s.length(), 0);
  CHECK(fmt::format("{}", empty_end) == "");

  string_view start(s.data(), 2);
  CHECK(fmt::format("{}", start) == "01");

  string_view middle(s.data() + 3, 4);
  CHECK(fmt::format("{}", middle) == "3456");

  string_view end(s.data() + s.length() - 2, 2);
  CHECK(fmt::format("{}", end) == "89");
}

TEST_SUITE_END();
