// Copyright (C) 2011-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/string.hpp>
#include <ccache/util/xxh3_128.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("util::XXH3_128");

TEST_CASE("util::XXH3_128")
{
  util::XXH3_128 checksum;
  auto digest = checksum.digest();
  CHECK(util::format_base16({digest.data(), 16})
        == "99aa06d3014798d86001c324468d497f");

  checksum.update(util::to_span("foo"));
  digest = checksum.digest();
  CHECK(util::format_base16({digest.data(), 16})
        == "79aef92e83454121ab6e5f64077e7d8a");

  checksum.update(util::to_span("t"));
  digest = checksum.digest();
  CHECK(util::format_base16({digest.data(), 16})
        == "e6045075b5bf1ae7a3e4c87775e6c97f");

  checksum.reset();
  digest = checksum.digest();
  CHECK(util::format_base16({digest.data(), 16})
        == "99aa06d3014798d86001c324468d497f");
}

TEST_SUITE_END();
