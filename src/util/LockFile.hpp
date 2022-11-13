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
#include <util/TimePoint.hpp>

#include <condition_variable>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace util {

class LockFile : NonCopyable
{
public:
  enum class Type { long_lived, short_lived };

  LockFile(const std::string& path, Type type);

  // Release the lock if previously acquired.
  ~LockFile();

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
  Type m_type;
  std::string m_alive_file;
  bool m_acquired;
  std::thread m_keep_alive_thread;
  std::mutex m_stop_keep_alive_mutex;
  bool m_stop_keep_alive = false;
  std::condition_variable m_stop_keep_alive_condition;
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

// A short-lived lock.
//
// The lock is expected to be released shortly after being acquired - if it is
// held for more than two seconds it risks being considered stale by another
// client.
class ShortLivedLockFile : public LockFile
{
public:
  ShortLivedLockFile(const std::string& path);
};

// A long-lived lock.
//
// The lock will (depending on implementation) be kept alive by a helper thread.
class LongLivedLockFile : public LockFile
{
public:
  LongLivedLockFile(const std::string& path);
};

inline LockFile::~LockFile()
{
  release();
}

inline ShortLivedLockFile::ShortLivedLockFile(const std::string& path)
  : LockFile(path, Type::short_lived)
{
}

inline LongLivedLockFile::LongLivedLockFile(const std::string& path)
  : LockFile(path, Type::long_lived)
{
}

} // namespace util
