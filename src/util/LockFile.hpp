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

#include <condition_variable>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace util {

class LockFile : NonCopyable
{
public:
  virtual ~LockFile() noexcept = default;

  // Acquire lock, blocking. Returns true if acquired, otherwise false.
  bool acquire();

  // Acquire lock, non-blocking. Returns true if acquired, otherwise false.
  bool try_acquire();

  // Release lock. If not previously acquired, nothing happens.
  void release();

  // Return whether the lock was acquired successfully.
  bool acquired() const;

protected:
  LockFile(const std::string& path);

private:
  std::string m_lock_file;
#ifndef _WIN32
  bool m_acquired;
#else
  void* m_handle;
#endif

  bool acquire(bool blocking);
  virtual void on_after_acquire();
  virtual void on_before_release();
#ifndef _WIN32
  bool do_acquire(bool blocking);
  virtual bool on_before_break();
  virtual std::optional<double> get_last_lock_update();
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

private:
#ifndef _WIN32
  std::string m_alive_file;
  std::thread m_keep_alive_thread;
  std::mutex m_stop_keep_alive_mutex;
  bool m_stop_keep_alive = false;
  std::condition_variable m_stop_keep_alive_condition;

  void on_after_acquire() override;
  void on_before_release() override;
  bool on_before_break() override;
  std::optional<double> get_last_lock_update() override;
#endif
};

class LockFileGuard : NonCopyable
{
public:
  enum class Mode { blocking, non_blocking };

  LockFileGuard(LockFile& lock_file, Mode mode = Mode::blocking);
  ~LockFileGuard() noexcept;

  bool acquired() const;

private:
  LockFile& m_lock_file;
};

inline void
LockFile::on_after_acquire()
{
}

inline void
LockFile::on_before_release()
{
}

#ifndef _WIN32

inline bool
LockFile::on_before_break()
{
  return true;
}

inline std::optional<double>
LockFile::get_last_lock_update()
{
  return std::nullopt;
}

#endif

} // namespace util
