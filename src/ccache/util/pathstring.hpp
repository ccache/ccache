// Copyright (C) 2024 Joel Rosdahl and other contributors
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

#include <filesystem>
#include <iterator>
#include <string>

namespace util {

// std::filesystem::path wrapper for accessing the underlying path string
// without having to copy it if std::filesystem::path::value_type is char (that
// is, not wchar_t).
template <typename CharT>
class BasicPathString
{
public:
  inline BasicPathString(const std::filesystem::path& path) : m_path(path) {}

  inline operator const std::basic_string<CharT>&() const { return str(); }
  inline const std::basic_string<CharT>& str() const { return m_path.native(); }
  inline const CharT* c_str() const { return str().c_str(); }
  inline std::basic_string<CharT>::iterator begin() const { return str().begin(); }
  inline std::basic_string<CharT>::iterator end() const { return str().end(); }

private:
  const std::filesystem::path& m_path;
};

#ifndef _WIN32
using PathString = BasicPathString<char>;
#else
using PathString = BasicPathString<char16_t>;
#endif

} // namespace util
