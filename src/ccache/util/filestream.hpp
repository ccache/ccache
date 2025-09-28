// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/noncopyable.hpp>
#include <ccache/util/path.hpp>

#include <cstdio>

namespace util {

class FileStream : util::NonCopyable
{
public:
  FileStream() = default;
  explicit FileStream(FILE* file);
  FileStream(const std::filesystem::path& path, const char* mode);
  FileStream(FileStream&& other) noexcept;
  ~FileStream();

  FileStream& operator=(FileStream&& other) noexcept;

  void open(const std::filesystem::path& path, const char* mode);
  void close();

  operator bool() const;
  FILE* operator*() const;
  FILE* get();

private:
  FILE* m_file = nullptr;
  bool m_owned = false;
};

inline FileStream::FileStream(FILE* const file)
  : m_file(file),
    m_owned(false)
{
}

inline FileStream::FileStream(const std::filesystem::path& path,
                              const char* mode)
{
  open(path, mode);
}

inline FileStream::FileStream(FileStream&& other) noexcept
  : m_file(other.m_file),
    m_owned(other.m_owned)
{
  other.m_file = nullptr;
  other.m_owned = false;
}

inline FileStream::~FileStream()
{
  close();
}

inline FileStream&
FileStream::operator=(FileStream&& other) noexcept
{
  m_file = other.m_file;
  m_owned = other.m_owned;
  other.m_file = nullptr;
  other.m_owned = false;
  return *this;
}

inline void
FileStream::open(const std::filesystem::path& path, const char* mode)
{
  close();
  m_file = fopen(util::pstr(path).c_str(), mode);
  m_owned = true;
}

inline void
FileStream::close()
{
  if (m_file && m_owned) {
    fclose(m_file);
    m_file = nullptr;
  }
  m_owned = false;
}

inline FileStream::operator bool() const
{
  return m_file;
}

inline FILE*
FileStream::operator*() const
{
  return m_file;
}

inline FILE*
FileStream::get()
{
  return m_file;
}

} // namespace util
