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

#include <ccache/util/noncopyable.hpp>

namespace util {

class FileLock : util::NonCopyable
{
public:
  explicit FileLock(int fd);
  FileLock(FileLock&& other) noexcept;

  FileLock& operator=(FileLock&& other) noexcept;

  // Release the lock if previously acquired.
  ~FileLock();

  // Acquire lock, blocking. Returns true if acquired, otherwise false.
  [[nodiscard]] bool acquire() noexcept;

  // Release lock early. If not previously acquired, nothing happens.
  void release() noexcept;

  // Return whether the lock is acquired successfully.
  bool acquired() const;

private:
  int m_fd = -1;
  bool m_acquired = false;
};

inline FileLock::~FileLock()
{
  release();
}

inline bool
FileLock::acquired() const
{
  return m_acquired;
}

} // namespace util
