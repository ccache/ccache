// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include <NonCopyable.hpp>
#include <util/LongLivedLockFileManager.hpp>
#include <util/TimePoint.hpp>

#include <optional>
#include <string>

namespace util {

// Unless make_long_lived is called, the lock is expected to be released shortly
// after being acquired - if it is held for more than two seconds it risks being
// considered stale by another client.
class LockFile : NonCopyable
{
public:
  explicit LockFile(const std::string& path);

  // Release the lock if previously acquired.
  ~LockFile();

  // Make this lock long-lived. Depending on implementation, it will be kept
  // alive by a helper thread.
  void make_long_lived(LongLivedLockFileManager& lock_manager);

  // Acquire lock, blocking. Returns true if acquired, otherwise false.
  bool acquire();

  // Acquire lock, non-blocking. Returns true if acquired, otherwise false.
  bool try_acquire();

  // Release lock early. If not previously acquired, nothing happens.
  void release();

  // Return whether the lock is acquired successfully.
  bool acquired() const;

private:
  std::string m_lock_file;
#ifndef _WIN32
  LongLivedLockFileManager* m_lock_manager = nullptr;
  std::string m_alive_file;
  bool m_acquired;
#else
  void* m_handle;
#endif

  bool acquire(bool blocking);
#ifndef _WIN32
  bool do_acquire(bool blocking);
  std::optional<util::TimePoint> get_last_lock_update();
#else
  void* do_acquire(bool blocking);
#endif
};

inline LockFile::~LockFile()
{
  release();
}

} // namespace util
