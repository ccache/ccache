// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/sourcescanner.hpp>

#include <doctest/doctest.h>

#include <string>

using sourcescanner::EmbedDirective;
using sourcescanner::scan_for_embed_directives;

TEST_SUITE_BEGIN("sourcescanner");

TEST_CASE("scan_for_embed_directives: empty source")
{
  auto result = scan_for_embed_directives("");
  CHECK(result.empty());
}

TEST_CASE("scan_for_embed_directives: no embed directives")
{
  auto result = scan_for_embed_directives(R"(
#include <stdio.h>
#include "header.h"
int main() { return 0; }
)");
  CHECK(result.empty());
}

TEST_CASE("scan_for_embed_directives: simple quoted embed")
{
  auto result = scan_for_embed_directives(R"(
#embed "data.bin"
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
  CHECK(result[0].is_system == false);
}

TEST_CASE("scan_for_embed_directives: simple system embed")
{
  auto result = scan_for_embed_directives(R"(
#embed <system_data.bin>
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "system_data.bin");
  CHECK(result[0].is_system == true);
}

TEST_CASE("scan_for_embed_directives: embed with path")
{
  auto result = scan_for_embed_directives(R"(
#embed "assets/textures/icon.png"
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "assets/textures/icon.png");
  CHECK(result[0].is_system == false);
}

TEST_CASE("scan_for_embed_directives: embed with parameters")
{
  auto result = scan_for_embed_directives(R"(
#embed "data.bin" limit(100)
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
  CHECK(result[0].is_system == false);
}

TEST_CASE("scan_for_embed_directives: embed with multiple parameters")
{
  auto result = scan_for_embed_directives(R"(
#embed "data.bin" prefix(0x00,) suffix(,0x00) if_empty(0) limit(256)
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
  CHECK(result[0].is_system == false);
}

TEST_CASE("scan_for_embed_directives: multiple embeds")
{
  auto result = scan_for_embed_directives(R"(
#include <stdio.h>
#embed "file1.bin"
int main() {
#embed "file2.bin"
#embed <system.bin>
  return 0;
}
)");
  REQUIRE(result.size() == 3);
  CHECK(result[0].path == "file1.bin");
  CHECK(result[0].is_system == false);
  CHECK(result[1].path == "file2.bin");
  CHECK(result[1].is_system == false);
  CHECK(result[2].path == "system.bin");
  CHECK(result[2].is_system == true);
}

TEST_CASE("scan_for_embed_directives: embed with whitespace")
{
  auto result = scan_for_embed_directives(R"(
#  embed   "data.bin"
)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
}

TEST_CASE("scan_for_embed_directives: embed with line continuation")
{
  auto result = scan_for_embed_directives("#embed \\\n\"data.bin\"\n");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
}

TEST_CASE("scan_for_embed_directives: embed at start of file")
{
  auto result = scan_for_embed_directives("#embed \"first.bin\"\n");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "first.bin");
}

TEST_CASE("scan_for_embed_directives: embed at end of file without newline")
{
  auto result = scan_for_embed_directives("#embed \"last.bin\"");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "last.bin");
}

TEST_CASE("scan_for_embed_directives: ignores embedded in identifier")
{
  auto result = scan_for_embed_directives(R"(
#embedded "not_this.bin"
#embedx "not_this_either.bin"
)");
  CHECK(result.empty());
}

TEST_CASE("scan_for_embed_directives: handles tabs")
{
  auto result = scan_for_embed_directives("#\tembed\t\"data.bin\"\n");
  REQUIRE(result.size() == 1);
  CHECK(result[0].path == "data.bin");
}

TEST_CASE("scan_for_embed_directives: mixed includes and embeds")
{
  auto result = scan_for_embed_directives(R"(
#include <stdio.h>
#include "local.h"
#embed "binary.dat"
#define FOO 1
#embed <sys/resource.bin>
#ifdef BAR
#embed "conditional.bin"
#endif
)");
  REQUIRE(result.size() == 3);
  CHECK(result[0].path == "binary.dat");
  CHECK(result[1].path == "sys/resource.bin");
  CHECK(result[2].path == "conditional.bin");
}

TEST_SUITE_END();
