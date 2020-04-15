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

#include "Tokenizer.hpp"

namespace util {

Tokenizer::Iterator
Tokenizer::Iterator::operator++()
{
  if (m_pos >= m_string.size()) {
    return *this;
  }

  m_pos = m_string.find_first_not_of(m_delimiters, m_pos + m_count);
  if (m_pos == nonstd::string_view::npos) {
    m_pos = m_string.size();
    m_count = 0;
  } else {
    m_count = m_string.substr(m_pos).find_first_of(m_delimiters);
    if (m_count == nonstd::string_view::npos) {
      m_count = m_string.size() - m_pos;
    }
  }
  return *this;
}

} // namespace util
