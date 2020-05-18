// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include <cstdio>
#include <string>

class File : public NonCopyable
{
public:
  File() = default;

  File(const std::string& path, const char* mode)
  {
    open(path, mode);
  }

  File(File&& other) : m_file(other.m_file)
  {
    other.m_file = nullptr;
  }

  File&
  operator=(File&& other)
  {
    m_file = other.m_file;
    other.m_file = nullptr;
    return *this;
  }

  void
  open(const std::string& path, const char* mode)
  {
    close();
    m_file = fopen(path.c_str(), mode);
  }

  void
  close()
  {
    if (m_file) {
      fclose(m_file);
      m_file = nullptr;
    }
  }

  ~File()
  {
    close();
  }

  operator bool() const
  {
    return m_file;
  }

  FILE*
  get()
  {
    return m_file;
  }

private:
  FILE* m_file = nullptr;
};
