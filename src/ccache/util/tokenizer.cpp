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

#include "tokenizer.hpp"

namespace util {

void
Tokenizer::Iterator::advance(bool initial)
{
  constexpr auto npos = std::string_view::npos;
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

std::string_view
Tokenizer::Iterator::operator*() const
{
  DEBUG_ASSERT(m_left <= m_right);
  DEBUG_ASSERT(m_right <= m_tokenizer.m_string.length());
  const bool include_delim =
    m_tokenizer.m_include_delimiter == IncludeDelimiter::yes;
  const int with_delim =
    include_delim && m_right < m_tokenizer.m_string.length() ? 1 : 0;
  return m_tokenizer.m_string.substr(m_left, m_right - m_left + with_delim);
}

} // namespace util
