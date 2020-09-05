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

#pragma once

#include "system.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/string_view.hpp"

class Arg
{
public:
  // Splits strings like "key=value" into key and value.
  Arg(nonstd::string_view full);
  Arg(nonstd::string_view key,
      const char split_char,
      nonstd::string_view value);

  Arg(const Arg&);
  Arg& operator=(const Arg&);

  // Enable passing arg as if it were a simple string.
  operator const nonstd::string_view() const;
  operator const std::string&() const;

  // When compared to a string literal behave as if arg were a simple string.
  bool operator==(const char* str) const;
  bool operator!=(const char* str) const;

  const std::string& full() const;
  nonstd::string_view key() const;
  nonstd::string_view value() const;

  bool has_been_split() const;
  char split_char() const;

private:
  // These point into m_full.
  nonstd::string_view m_key, m_value;
  std::string m_full;

  // Will support ' ' in the future like "-key value".
  char m_split_char;
};

// Specialization of fmt::formatter for Arg
namespace fmt {

template<> struct formatter<Arg>
{
  template<typename ParseContext>
  constexpr auto
  parse(ParseContext& ctx) const -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template<typename FormatContext>
  auto
  format(const Arg& arg, FormatContext& ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", arg.full());
  }
};

} // namespace fmt

inline Arg::operator const nonstd::string_view() const
{
  return full();
}

inline Arg::operator const std::string&() const
{
  return full();
}

inline bool
Arg::operator==(const char* str) const
{
  return full() == str;
}

inline bool
Arg::operator!=(const char* str) const
{
  return full() != str;
}

inline const std::string&
Arg::full() const
{
  return m_full;
}

inline nonstd::string_view
Arg::key() const
{
  return m_key;
}

inline nonstd::string_view
Arg::value() const
{
  return m_value;
}

inline bool
Arg::has_been_split() const
{
  return m_split_char != 0;
}

inline char
Arg::split_char() const
{
  return m_split_char;
}

inline bool
operator==(const Arg& lhs, const Arg& rhs)
{
  return lhs.full() == rhs.full() && lhs.key() == rhs.key()
         && lhs.value() == rhs.value() && lhs.split_char() == rhs.split_char();
}
