// Copyright (C) 2024-2025 Joel Rosdahl and other contributors
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

#include "filelock.hpp"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace util {

FileLock::FileLock(int fd)
  : m_fd(fd)
{
}

FileLock::FileLock(FileLock&& other) noexcept
  : m_fd(other.m_fd),
    m_acquired(other.m_acquired)
{
  other.m_fd = -1;
  other.m_acquired = false;
}

FileLock&
FileLock::operator=(FileLock&& other) noexcept
{
  if (&other != this) {
    release();
    m_fd = other.m_fd;
    m_acquired = other.m_acquired;
    other.m_fd = -1;
    other.m_acquired = false;
  }
  return *this;
}

bool
FileLock::acquire() noexcept
{
  if (m_fd == -1) {
    return false;
  }

#ifdef _WIN32
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(m_fd));
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  OVERLAPPED overlapped{};
  m_acquired =
    LockFileEx(
      handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlapped)
    != 0;
#else
  struct flock lock;
  memset(&lock, 0, sizeof(lock));
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  int result;
  do {
    result = fcntl(m_fd, F_SETLKW, &lock);
  } while (result != 0 && errno == EINTR);
  m_acquired = result == 0;
#endif
  return m_acquired;
}

void
FileLock::release() noexcept
{
  if (!acquired() || m_fd == -1) {
    return;
  }

#ifdef _WIN32
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(m_fd));
  if (handle != INVALID_HANDLE_VALUE) {
    OVERLAPPED overlapped{};
    UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped);
  }
#else
  struct flock lock;
  memset(&lock, 0, sizeof(lock));
  lock.l_type = F_UNLCK;
  lock.l_whence = SEEK_SET;
  int result;
  do {
    result = fcntl(m_fd, F_SETLK, &lock);
  } while (result != 0 && errno == EINTR);
#endif
  m_acquired = false;
}

} // namespace util
