// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/assertions.hpp>

#include <cstddef>
#include <string_view>

namespace util {

// An instance of this class can be used in a range-based for loop to split a
// string into tokens at any of the characters in a string of delimiters.
template <typename CharT>
class BasicTokenizer
{
public:
  enum class Mode {
    include_empty, // Include empty tokens.
    skip_empty,    // Skip empty tokens.
  };

  enum class IncludeDelimiter { no, yes };

  // Split `string` into tokens at any of the characters in `separators` which
  // must neither be the empty string nor a nullptr.
  inline BasicTokenizer(std::basic_string_view<CharT> string,
                        const CharT* delimiters,
                        Mode mode = Mode::skip_empty,
                        IncludeDelimiter include_delimiter = IncludeDelimiter::no)
                        : m_string(string),
                          m_delimiters(delimiters),
                          m_mode(mode),
                          m_include_delimiter(include_delimiter)
  {
    DEBUG_ASSERT(delimiters != nullptr && delimiters[0]);
  }

  class Iterator
  {
  public:
    inline Iterator(const BasicTokenizer& tokenizer, size_t start_pos)
                    : m_tokenizer(tokenizer),
                      m_left(start_pos),
                      m_right(start_pos)
    {
      if (start_pos == 0) {
        advance(true);
      } else {
        DEBUG_ASSERT(start_pos == std::basic_string_view<CharT>::npos);
      }
    }

    inline Iterator operator++()
    {
      advance(false);
      return *this;
    }

    inline bool operator!=(const Iterator& other) const
    {
      return &m_tokenizer != &other.m_tokenizer || m_left != other.m_left;
    }

    std::basic_string_view<CharT> operator*() const
    {
      DEBUG_ASSERT(m_left <= m_right);
      DEBUG_ASSERT(m_right <= m_tokenizer.m_string.length());
      const bool include_delim =
        m_tokenizer.m_include_delimiter == IncludeDelimiter::yes;
      const int with_delim =
        include_delim && m_right < m_tokenizer.m_string.length() ? 1 : 0;
      return m_tokenizer.m_string.substr(m_left, m_right - m_left + with_delim);
    }

  private:
    const BasicTokenizer& m_tokenizer;
    size_t m_left;
    size_t m_right;

    void advance(bool initial)
    {
      constexpr auto npos = std::basic_string_view<CharT>::npos;
      const auto string = m_tokenizer.m_string;
      const auto delimiters = m_tokenizer.m_delimiters;
      const auto mode = m_tokenizer.m_mode;

      DEBUG_ASSERT(m_left <= m_right);
      DEBUG_ASSERT(m_right <= string.length());

      do {
        if (initial) {
          initial = false;
        } else if (m_right == string.length()) {
          m_left = npos;
        } else {
          m_left = m_right + 1;
        }
        if (m_left != npos) {
          const auto delim_pos = string.find_first_of(delimiters, m_left);
          m_right = delim_pos == npos ? string.length() : delim_pos;
        }
      } while (mode == Mode::skip_empty && m_left == m_right);
    }
  };

  inline Iterator begin()
  {
    return Iterator(*this, 0);
  }

  inline Iterator end()
  {
    return Iterator(*this, std::string_view::npos);
  }

private:
  friend Iterator;

  const std::basic_string_view<CharT> m_string;
  const CharT* const m_delimiters;
  const Mode m_mode;
  const IncludeDelimiter m_include_delimiter;
};

using Tokenizer = BasicTokenizer<char>;

} // namespace util
