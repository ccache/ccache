// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include "NonCopyable.hpp"

#include <string>

class File : public NonCopyable
{
public:
  File() = default;
  File(const std::string& path, const char* mode);
  File(File&& other) noexcept;
  ~File();

  File& operator=(File&& other) noexcept;

  void open(const std::string& path, const char* mode);
  void close();

  enum class Seek {
    Set = SEEK_SET,
    Cur = SEEK_CUR,
    End = SEEK_END,
  };

  int seek(int64_t offset, Seek origin);
  int64_t size() const;

  operator bool() const;
  FILE* operator*() const;
  FILE* get();

private:
  FILE* m_file = nullptr;
};

inline File::File(const std::string& path, const char* mode)
{
  open(path, mode);
}

inline File::File(File&& other) noexcept : m_file(other.m_file)
{
  other.m_file = nullptr;
}

inline File::~File()
{
  close();
}

inline File&
File::operator=(File&& other) noexcept
{
  m_file = other.m_file;
  other.m_file = nullptr;
  return *this;
}

inline void
File::open(const std::string& path, const char* mode)
{
  close();
  m_file = fopen(path.c_str(), mode);
}

inline void
File::close()
{
  if (m_file) {
    fclose(m_file);
    m_file = nullptr;
  }
}

inline int
File::seek(int64_t offset, Seek origin)
{
  if (!m_file) {
    return -1;
  }
#ifdef _WIN32
  return ::_fseeki64(m_file, offset, static_cast<int>(origin));
#else
  return ::fseeko(m_file, offset, static_cast<int>(origin));
#endif
}

inline int64_t
File::size() const
{
  if (!m_file) {
    return -1;
  }
#ifdef _WIN32
  return ::_filelengthi64(::fileno(m_file));
#else
  struct stat st;
  if (::fstat(::fileno(m_file), &st)) {
    return -1;
  }
  return st.st_size;
#endif
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
