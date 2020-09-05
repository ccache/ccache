// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "Arg.hpp"

static const std::string emptyString;

Arg::Arg(nonstd::string_view full) : m_full(full), m_split_char(0)
{
  const size_t sep_pos = m_full.find('=');
  if (sep_pos != nonstd::string_view::npos) {
    m_split_char = '=';
    m_key = nonstd::string_view(m_full).substr(0, sep_pos);
    m_value = nonstd::string_view(m_full).substr(sep_pos + 1);
  }
}

Arg::Arg(const Arg& other)
  : m_full(other.m_full), m_split_char(other.m_split_char)
{
  if (other.has_been_split()) {
    m_key = nonstd::string_view(m_full).substr(0, other.key().size());
    m_value = nonstd::string_view(m_full).substr(other.key().size() + 1);
  }
  assert(*this == other);
}

Arg&
Arg::operator=(const Arg& other)
{
  m_full = other.full();
  m_split_char = other.m_split_char;
  if (other.has_been_split()) {
    m_key = nonstd::string_view(m_full).substr(0, other.key().size());
    m_value = nonstd::string_view(m_full).substr(other.key().size() + 1);
  }
  assert(*this == other);
  return *this;
}
