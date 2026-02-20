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

using sourcescanner::contains_embed_directive;
using sourcescanner::contains_incbin_directive;

TEST_SUITE_BEGIN("sourcescanner");

TEST_CASE("contains_embed_directive: empty source")
{
  CHECK_FALSE(contains_embed_directive(""));
}

TEST_CASE("contains_embed_directive: no embed directives")
{
  CHECK_FALSE(contains_embed_directive(R"(
#include <stdio.h>
#include "header.h"
int main() { return 0; }
)"));
}

TEST_CASE("contains_embed_directive: simple quoted embed")
{
  CHECK(contains_embed_directive(R"(
#embed "data.bin"
)"));
}

TEST_CASE("contains_embed_directive: simple system embed")
{
  CHECK(contains_embed_directive(R"(
#embed <system_data.bin>
)"));
}

TEST_CASE("contains_embed_directive: embed with path")
{
  CHECK(contains_embed_directive(R"(
#embed "assets/textures/icon.png"
)"));
}

TEST_CASE("contains_embed_directive: embed with parameters")
{
  CHECK(contains_embed_directive(R"(
#embed "data.bin" limit(100)
)"));
}

TEST_CASE("contains_embed_directive: embed with multiple parameters")
{
  CHECK(contains_embed_directive(R"(
#embed "data.bin" prefix(0x00,) suffix(,0x00) if_empty(0) limit(256)
)"));
}

TEST_CASE("contains_embed_directive: multiple embeds")
{
  CHECK(contains_embed_directive(R"(
#include <stdio.h>
#embed "file1.bin"
int main() {
#embed "file2.bin"
#embed <system.bin>
  return 0;
}
)"));
}

TEST_CASE("contains_embed_directive: embed with whitespace")
{
  CHECK(contains_embed_directive(R"(
#  embed   "data.bin"
)"));
}

TEST_CASE("contains_embed_directive: embed with line continuation")
{
  CHECK(contains_embed_directive(R"(#embed \
"data.bin"
)"));
}

TEST_CASE("contains_embed_directive: embed at start of file")
{
  CHECK(contains_embed_directive("#embed \"first.bin\"\n"));
}

TEST_CASE("contains_embed_directive: embed at end of file without newline")
{
  CHECK(contains_embed_directive("#embed \"last.bin\""));
}

TEST_CASE("contains_embed_directive: ignores embedded in identifier")
{
  CHECK_FALSE(contains_embed_directive(R"(
#embedded "not_this.bin"
#embedx "not_this_either.bin"
)"));
}

TEST_CASE("contains_embed_directive: handles tabs")
{
  CHECK(contains_embed_directive("#\tembed\t\"data.bin\"\n"));
}

TEST_CASE("contains_embed_directive: mixed includes and embeds")
{
  CHECK(contains_embed_directive(R"(
#include <stdio.h>
#include "local.h"
#embed "binary.dat"
#define FOO 1
#embed <sys/resource.bin>
#ifdef BAR
#embed "conditional.bin"
#endif
)"));
}

TEST_CASE("contains_incbin_directive: empty source")
{
  CHECK_FALSE(contains_incbin_directive(""));
}

TEST_CASE("contains_incbin_directive: no incbin directive")
{
  CHECK_FALSE(contains_incbin_directive(R"(
    #include <stdio.h>
    .incbin data.bin
  )"));
}

TEST_CASE("contains_incbin_directive: simple incbin")
{
  CHECK(contains_incbin_directive(".incbin \"data.bin\"\n"));
}

TEST_CASE("contains_incbin_directive: incbin without space")
{
  CHECK(contains_incbin_directive(".incbin\"data.bin\"\n"));
}

TEST_CASE("contains_incbin_directive: escaped quote")
{
  CHECK(contains_incbin_directive(".incbin \\\"data.bin\\\"\n"));
}

TEST_SUITE_END();
