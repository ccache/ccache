// Copyright (C) 2024 Joel Rosdahl and other contributors
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

#include <filesystem>
#include <string>

namespace util {

// std::filesystem::path wrapper for accessing the underlying path string
// without having to copy it if std::filesystem::path::value_type is char (that
// is, not wchar_t).
class PathString
{
public:
  PathString(const std::filesystem::path& path);

  operator const std::string&() const;

  const std::string& str() const;
  const char* c_str() const;

private:
#ifdef _WIN32
  std::string m_path;
#else
  const std::filesystem::path& m_path;
#endif
};

inline PathString::PathString(const std::filesystem::path& path)
#ifdef _WIN32
  : m_path(path.string())
#else
  : m_path(path)
#endif
{
}

inline PathString::operator const std::string&() const
{
  return str();
}

inline const std::string&
PathString::str() const
{
#ifdef _WIN32
  return m_path;
#else
  return m_path.native();
#endif
}

inline const char*
PathString::c_str() const
{
  return str().c_str();
}

} // namespace util
