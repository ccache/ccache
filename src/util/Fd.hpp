// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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
#include <util/assertions.hpp>
#include <util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace util {

class Fd : util::NonCopyable
{
public:
  Fd() = default;
  explicit Fd(int fd);
  Fd(Fd&& other_fd) noexcept;
  ~Fd();

  explicit operator bool() const;

  int get() const;
  int operator*() const;

  Fd& operator=(Fd&& other_fd) noexcept;

  // Close wrapped fd before the lifetime of Fd has ended.
  bool close();

  // Release ownership of wrapped fd.
  int release();

private:
  int m_fd = -1;
};

inline Fd::Fd(int fd) : m_fd(fd)
{
}

inline Fd::Fd(Fd&& other_fd) noexcept : m_fd(other_fd.release())
{
}

inline Fd::~Fd()
{
  close();
}

inline Fd::operator bool() const
{
  return m_fd != -1;
}

inline int
Fd::get() const
{
  return m_fd;
}

inline int
Fd::operator*() const
{
  ASSERT(m_fd != -1);
  return m_fd;
}

inline Fd&
Fd::operator=(Fd&& other_fd) noexcept
{
  close();
  m_fd = other_fd.release();
  return *this;
}

inline bool
Fd::close()
{
  return m_fd != -1 && ::close(release()) == 0;
}

inline int
Fd::release()
{
  int fd = m_fd;
  m_fd = -1;
  return fd;
}

} // namespace util
