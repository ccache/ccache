// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <third_party/nonstd/optional.hpp>
#include <third_party/nonstd/string_view.hpp>

namespace util {

class Tokenizer
{
public:
  Tokenizer(nonstd::string_view string, const char* delimiters);

  class Iterator
  {
  public:
    Iterator(nonstd::string_view string,
             const char* delimiters,
             size_t start_pos);

    Iterator operator++();
    bool operator!=(const Iterator& other) const;
    nonstd::string_view operator*() const;

  private:
    const nonstd::string_view m_string;
    const char* const m_delimiters;
    size_t m_pos;
    size_t m_count = 0;
  };

  Iterator begin();
  Iterator end();

private:
  const nonstd::string_view m_string;
  const char* const m_delimiters;
};

inline Tokenizer::Tokenizer(const nonstd::string_view string,
                            const char* const delimiters)
  : m_string(string),
    m_delimiters(delimiters)
{
  assert(delimiters != nullptr && delimiters[0] != '\0');
}

inline Tokenizer::Iterator::Iterator(const nonstd::string_view string,
                                     const char* const delimiters,
                                     const size_t start_pos)
  : m_string(string),
    m_delimiters(delimiters),
    m_pos(start_pos)
{
  ++*this;
}

inline bool
Tokenizer::Iterator::operator!=(const Iterator& other) const
{
  assert(m_string.data() == other.m_string.data());
  assert(m_delimiters == other.m_delimiters);
  return m_pos != other.m_pos || m_count != other.m_count;
}

inline nonstd::string_view
Tokenizer::Iterator::operator*() const
{
  assert(m_pos < m_string.size());
  return m_string.substr(m_pos, m_count);
}

inline Tokenizer::Iterator
Tokenizer::begin()
{
  return Iterator(m_string, m_delimiters, 0);
}

inline Tokenizer::Iterator
Tokenizer::end()
{
  return Iterator(m_string, m_delimiters, m_string.size());
}

} // namespace util
