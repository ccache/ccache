// Copyright (C) 2010-2024 Joel Rosdahl and other contributors
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

#include <ccache/hash.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("Hash");

TEST_CASE("known strings")
{
  SUBCASE("initial state")
  {
    CHECK(util::format_digest(Hash().digest())
          == "af1396svbud1kqg40jfa6reciicrpcisi");
  }

  SUBCASE("empty string")
  {
    CHECK(util::format_digest(Hash().hash("").digest())
          == "af1396svbud1kqg40jfa6reciicrpcisi");
  }

  SUBCASE("a")
  {
    CHECK(util::format_digest(Hash().hash("a").digest())
          == "17765vetiqd4ae95qpbhfb1ut8gj42r6m");
  }

  SUBCASE("message digest")
  {
    CHECK(util::format_digest(Hash().hash("message digest").digest())
          == "7bc2kbnbinerv6ruptldpdrb8ko93hcdo");
  }

  SUBCASE("long string")
  {
    const char long_string[] =
      "123456789012345678901234567890123456789012345678901234567890"
      "12345678901234567890";
    CHECK(util::format_digest(Hash().hash(long_string).digest())
          == "f263ljqhc8co1ee8rpeq98bt654o9o2qm");
  }
}

TEST_CASE("Hash::digest should not alter state")
{
  Hash h;
  h.hash("message");
  h.digest();
  h.hash(" digest");
  CHECK(util::format_digest(h.digest()) == "7bc2kbnbinerv6ruptldpdrb8ko93hcdo");
}

TEST_CASE("Hash::digest should be idempotent")
{
  Hash h;
  CHECK(util::format_digest(h.digest()) == "af1396svbud1kqg40jfa6reciicrpcisi");
  CHECK(util::format_digest(h.digest()) == "af1396svbud1kqg40jfa6reciicrpcisi");
}

TEST_SUITE_END();
