// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <doctest/doctest.h>

#include <ostream> // https://github.com/doctest/doctest/issues/618
#include <string>
#include <vector>

TEST_CASE("util::Tokenizer")
{
  using Mode = util::Tokenizer::Mode;
  using IncludeDelimiter = util::Tokenizer::IncludeDelimiter;
  struct SplitTest
  {
    SplitTest(Mode mode,
              IncludeDelimiter include_delimiter = IncludeDelimiter::no)
      : m_mode(mode),
        m_include_delimiter(include_delimiter)
    {
    }

    void
    operator()(const char* input,
               const char* separators,
               const std::vector<std::string>& expected) const
    {
      const auto res =
        util::split_into_views(input, separators, m_mode, m_include_delimiter);
      REQUIRE(res.size() == expected.size());
      for (size_t i = 0, total = expected.size(); i < total; ++i) {
        CHECK(res[i] == expected[i]);
      }
    }

    Mode m_mode;
    IncludeDelimiter m_include_delimiter;
  };

  SUBCASE("include empty tokens")
  {
    SplitTest split(Mode::include_empty);
    split("", "/", {""});
    split("/", "/", {"", ""});
    split("a/", "/", {"a", ""});
    split("/b", "/", {"", "b"});
    split("a/b", "/", {"a", "b"});
    split("/a:", "/:", {"", "a", ""});
  }

  SUBCASE("skip empty")
  {
    SplitTest split(Mode::skip_empty);
    split("", "/", {});
    split("///", "/", {});
    split("a/b", "/", {"a", "b"});
    split("a/b", "x", {"a/b"});
    split("a/b:c", "/:", {"a", "b", "c"});
    split("/a:", "/:", {"a"});
    split(":a//b..:.c/:/.", "/:.", {"a", "b", "c"});
    split(".0.1.2.3.4.5.6.7.8.9.",
          "/:.+_abcdef",
          {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});
  }

  SUBCASE("include empty and delimiter")
  {
    SplitTest split(Mode::include_empty, IncludeDelimiter::yes);
    split("", "/", {""});
    split("/", "/", {"/", ""});
    split("a/", "/", {"a/", ""});
    split("/b", "/", {"/", "b"});
    split("a/b", "/", {"a/", "b"});
    split("/a:", "/:", {"/", "a:", ""});
    split("a//b/", "/", {"a/", "/", "b/", ""});
  }

  SUBCASE("skip empty and include delimiter")
  {
    SplitTest split(Mode::skip_empty, IncludeDelimiter::yes);
    split("", "/", {});
    split("///", "/", {});
    split("a/b", "/", {"a/", "b"});
    split("a/b", "x", {"a/b"});
    split("a/b:c", "/:", {"a/", "b:", "c"});
    split("/a:", "/:", {"a:"});
    split(":a//b..:.c/:/.", "/:.", {"a/", "b.", "c/"});
    split(".0.1.2.3.4.5.6.7.8.9.",
          "/:.+_abcdef",
          {"0.", "1.", "2.", "3.", "4.", "5.", "6.", "7.", "8.", "9."});
  }
}
