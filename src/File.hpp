// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include <util/NonCopyable.hpp>

#include <cstdio>
#include <string>

class File : util::NonCopyable
{
public:
  File() = default;
  explicit File(FILE* file);
  File(const std::string& path, const char* mode);
  File(File&& other) noexcept;
  ~File();

  File& operator=(File&& other) noexcept;

  void open(const std::string& path, const char* mode);
  void close();

  operator bool() const;
  FILE* operator*() const;
  FILE* get();

private:
  FILE* m_file = nullptr;
  bool m_owned = false;
};

inline File::File(FILE* const file) : m_file(file), m_owned(false)
{
}

inline File::File(const std::string& path, const char* mode)
{
  open(path, mode);
}

inline File::File(File&& other) noexcept
  : m_file(other.m_file),
    m_owned(other.m_owned)
{
  other.m_file = nullptr;
  other.m_owned = false;
}

inline File::~File()
{
  close();
}

inline File&
File::operator=(File&& other) noexcept
{
  m_file = other.m_file;
  m_owned = other.m_owned;
  other.m_file = nullptr;
  other.m_owned = false;
  return *this;
}

inline void
File::open(const std::string& path, const char* mode)
{
  close();
  m_file = fopen(path.c_str(), mode);
  m_owned = true;
}

inline void
File::close()
{
  if (m_file && m_owned) {
    fclose(m_file);
    m_file = nullptr;
  }
  m_owned = false;
}

inline File::operator bool() const
{
  return m_file;
}

inline FILE*
File::operator*() const
{
  return m_file;
}

inline FILE*
File::get()
{
  return m_file;
}
