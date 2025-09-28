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

#pragma once

#include <ccache/util/assertions.hpp>

#include <cstddef>
#include <string_view>

namespace util {

// An instance of this class can be used in a range-based for loop to split a
// string into tokens at any of the characters in a string of delimiters.
class Tokenizer
{
public:
  enum class Mode {
    include_empty, // Include empty tokens.
    skip_empty,    // Skip empty tokens.
  };

  enum class IncludeDelimiter { no, yes };

  // Split `string` into tokens at any of the characters in `separators` which
  // must neither be the empty string nor a nullptr.
  Tokenizer(std::string_view string,
            const char* delimiters,
            Mode mode = Mode::skip_empty,
            IncludeDelimiter include_delimiter = IncludeDelimiter::no);

  class Iterator
  {
  public:
    Iterator(const Tokenizer& tokenizer, size_t start_pos);

    Iterator operator++();
    bool operator!=(const Iterator& other) const;
    std::string_view operator*() const;

  private:
    const Tokenizer& m_tokenizer;
    size_t m_left;
    size_t m_right;

    void advance(bool initial);
  };

  Iterator begin();
  Iterator end();

private:
  friend Iterator;

  const std::string_view m_string;
  const char* const m_delimiters;
  const Mode m_mode;
  const IncludeDelimiter m_include_delimiter;
};

inline Tokenizer::Tokenizer(const std::string_view string,
                            const char* const delimiters,
                            Tokenizer::Mode mode,
                            Tokenizer::IncludeDelimiter include_delimiter)
  : m_string(string),
    m_delimiters(delimiters),
    m_mode(mode),
    m_include_delimiter(include_delimiter)
{
  DEBUG_ASSERT(delimiters != nullptr && delimiters[0] != '\0');
}

inline Tokenizer::Iterator::Iterator(const Tokenizer& tokenizer,
                                     const size_t start_pos)
  : m_tokenizer(tokenizer),
    m_left(start_pos),
    m_right(start_pos)
{
  if (start_pos == 0) {
    advance(true);
  } else {
    DEBUG_ASSERT(start_pos == std::string_view::npos);
  }
}

inline Tokenizer::Iterator
Tokenizer::Iterator::operator++()
{
  advance(false);
  return *this;
}

inline bool
Tokenizer::Iterator::operator!=(const Iterator& other) const
{
  return &m_tokenizer != &other.m_tokenizer || m_left != other.m_left;
}

inline Tokenizer::Iterator
Tokenizer::begin()
{
  return Iterator(*this, 0);
}

inline Tokenizer::Iterator
Tokenizer::end()
{
  return Iterator(*this, std::string_view::npos);
}

} // namespace util
