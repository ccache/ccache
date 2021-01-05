// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "../src/third_party/llvm_yaml_escape.hpp"

#include "third_party/doctest.h"

TEST_CASE("only 7 bit")
{
  std::string in = R"(fo"o\bar)";
  in.push_back('\0');
  in.push_back('Z');
  in.push_back('\n');
  std::string out = llvm::yaml::escape(in);

  CHECK(out == R"(fo\"o\\bar\0Z\n)");
}

TEST_CASE("2 byte utf8")
{
  std::string in = "1.7 Ångström";
  std::string out = llvm::yaml::escape(in);

  CHECK(out == R"(1.7 \xC5ngstr\xF6m)");
}

TEST_CASE("emoji utf8")
{
  std::string in = "🖥️💨";
  std::string out = llvm::yaml::escape(in);

  CHECK(out == R"(\U0001F5A5\uFE0F\U0001F4A8)");
}
