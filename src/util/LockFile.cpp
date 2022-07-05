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

#include "LockFile.hpp"

#include "Logging.hpp"
#include "Util.hpp"
#include "Win32Util.hpp"
#include "fmtmacros.hpp"

#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <util/file.hpp>

#include "third_party/fmt/core.h"

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <random>
#include <sstream>

// Seconds.
const double k_min_sleep_time = 0.010;
const double k_max_sleep_time = 0.050;
const double k_staleness_limit = 2;
const double k_keep_alive_interval = k_staleness_limit / 4;
const auto k_keep_alive_interval_ms = std::chrono::milliseconds{
  static_cast<uint64_t>(k_keep_alive_interval * 1000)};

namespace {

class RandomNumberGenerator
{
public:
  RandomNumberGenerator(int32_t min, int32_t max)
    : m_random_engine(m_random_device()),
      m_distribution(min, max)
  {
  }

  int32_t
  get()
  {
    return m_distribution(m_random_engine);
  };

private:
  std::random_device m_random_device;
  std::default_random_engine m_random_engine;
  std::uniform_int_distribution<int32_t> m_distribution;
};

} // namespace

namespace util {

LockFile::LockFile(const std::string& path)
  : m_lock_file(path + ".lock"),
#ifndef _WIN32
    m_acquired(false)
#else
    m_handle(INVALID_HANDLE_VALUE)
#endif
{
}

bool
LockFile::acquire()
{
  LOG("Acquiring {}", m_lock_file);
  return acquire(true);
}

bool
LockFile::try_acquire()
{
  LOG("Trying to acquire {}", m_lock_file);
  return acquire(false);
}

void
LockFile::release()
{
  if (!acquired()) {
    return;
  }

  LOG("Releasing {}", m_lock_file);
#ifndef _WIN32
  on_before_release();
  Util::unlink_tmp(m_lock_file);
#else
  CloseHandle(m_handle);
#endif
  LOG("Released {}", m_lock_file);
#ifndef _WIN32
  m_acquired = false;
#else
  m_handle = INVALID_HANDLE_VALUE;
#endif
}

bool
LockFile::acquired() const
{
#ifndef _WIN32
  return m_acquired;
#else
  return m_handle != INVALID_HANDLE_VALUE;
#endif
}

bool
LockFile::acquire(const bool blocking)
{
  ASSERT(!acquired());

#ifndef _WIN32
  m_acquired = do_acquire(blocking);
#else
  m_handle = do_acquire(blocking);
#endif
  if (acquired()) {
    LOG("Acquired {}", m_lock_file);
    on_after_acquire();
  } else {
    LOG("Failed to acquire lock {}", m_lock_file);
  }

  return acquired();
}

#ifndef _WIN32

static double
time_from_clock()
{
  timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + static_cast<double>(tv.tv_usec) / 1'000'000;
}

static double
time_from_stat(const Stat& stat)
{
  const auto mtime = stat.mtim();
  return mtime.tv_sec + static_cast<double>(mtime.tv_nsec) / 1'000'000'000;
}

bool
LockFile::do_acquire(const bool blocking)
{
  std::stringstream ss;
  ss << Util::get_hostname() << '-' << getpid() << '-'
     << std::this_thread::get_id();
  const auto content_prefix = ss.str();

  double last_seen_activity = [this] {
    const auto last_lock_update = get_last_lock_update();
    return last_lock_update ? *last_lock_update : time_from_clock();
  }();

  std::string initial_content;
  RandomNumberGenerator sleep_ms_generator(k_min_sleep_time * 1000,
                                           k_max_sleep_time * 1000);

  while (true) {
    const auto my_content = FMT("{}-{}", content_prefix, time_from_clock());

    if (symlink(my_content.c_str(), m_lock_file.c_str()) == 0) {
      // We got the lock.
      return true;
    }

    int saved_errno = errno;
    LOG("Could not acquire {}: {}", m_lock_file, strerror(saved_errno));
    if (saved_errno == ENOENT) {
      // Directory doesn't exist?
      if (Util::create_dir(Util::dir_name(m_lock_file))) {
        // OK. Retry.
        continue;
      }
    }

    if (saved_errno == EPERM) {
      // The file system does not support symbolic links. We have no choice but
      // to grant the lock anyway.
      return true;
    }

    if (saved_errno != EEXIST) {
      // Directory doesn't exist or isn't writable?
      return false;
    }

    std::string content = Util::read_link(m_lock_file);
    if (content.empty()) {
      if (errno == ENOENT) {
        // The symlink was removed after the symlink() call above, so retry
        // acquiring it.
        continue;
      } else {
        LOG("Could not read symlink {}: {}", m_lock_file, strerror(errno));
        return false;
      }
    }

    if (content == my_content) {
      // Lost NFS reply?
      LOG("Symlinking {} failed but we got the lock anyway", m_lock_file);
      return true;
    }

    LOG("Lock info for {}: {}", m_lock_file, content);

    if (initial_content.empty()) {
      initial_content = content;
    }

    const auto last_lock_update = get_last_lock_update();
    if (last_lock_update) {
      last_seen_activity = std::max(last_seen_activity, *last_lock_update);
    }

    const double inactive_duration = time_from_clock() - last_seen_activity;

    if (inactive_duration < k_staleness_limit) {
      LOG("Lock {} held by another process active {:.3f} seconds ago",
          m_lock_file,
          inactive_duration);
      if (!blocking) {
        return false;
      }
    } else if (content == initial_content) {
      // The lock seems to be stale -- break it and try again.
      LOG("Breaking {} since it has been inactive for {:.3f} seconds",
          m_lock_file,
          inactive_duration);
      if (!on_before_break() || !Util::unlink_tmp(m_lock_file)) {
        return false;
      }

      // Note: There is an inherent race condition here where two processes may
      // believe they both acquired the lock after breaking it:
      //
      // 1. A decides to break the lock.
      // 2. B decides to break the lock.
      // 3. A removes the file and retries.
      // 4. A acquires the lock.
      // 5. B removes the file and retries.
      // 6. B acquires the lock.
      //
      // To reduce the risk we sleep for a while before retrying so that it's
      // likely that step 5 happens before step 4.
    } else {
      LOG("Lock {} reacquired by another process", m_lock_file);
      if (!blocking) {
        return false;
      }
      initial_content = content;
    }

    const std::chrono::milliseconds to_sleep{sleep_ms_generator.get()};
    LOG("Sleeping {} ms", to_sleep.count());
    std::this_thread::sleep_for(to_sleep);
  }
}

#else // !_WIN32

void*
LockFile::do_acquire(const bool blocking)
{
  void* handle;
  RandomNumberGenerator sleep_ms_generator(k_min_sleep_time * 1000,
                                           k_max_sleep_time * 1000);

  while (true) {
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE;
    handle = CreateFile(m_lock_file.c_str(),
                        GENERIC_WRITE, // desired access
                        0,             // shared mode (0 = not shared)
                        nullptr,       // security attributes
                        CREATE_ALWAYS, // creation disposition
                        flags,         // flags and attributes
                        nullptr        // template file
    );
    if (handle != INVALID_HANDLE_VALUE) {
      break;
    }

    DWORD error = GetLastError();
    if (error == ERROR_PATH_NOT_FOUND) {
      // Directory doesn't exist?
      if (Util::create_dir(Util::dir_name(m_lock_file))) {
        // OK. Retry.
        continue;
      }
    }

    LOG("Could not acquire {}: {} ({})",
        m_lock_file,
        Win32Util::error_message(error),
        error);

    // ERROR_SHARING_VIOLATION: lock already held.
    // ERROR_ACCESS_DENIED: maybe pending delete.
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) {
      // Fatal error, give up.
      break;
    }

    LOG("Lock {} held by another process", m_lock_file);
    if (!blocking) {
      break;
    }

    const std::chrono::milliseconds to_sleep{sleep_ms_generator.get()};
    LOG("Sleeping {} ms", to_sleep.count());
    std::this_thread::sleep_for(to_sleep);
  }

  return handle;
}

#endif // !_WIN32

ShortLivedLockFile::ShortLivedLockFile(const std::string& path) : LockFile(path)
{
}

LongLivedLockFile::LongLivedLockFile(const std::string& path)
  : LockFile(path)
#ifndef _WIN32
    ,
    m_alive_file(path + ".alive")
#endif
{
}

#ifndef _WIN32

void
LongLivedLockFile::on_after_acquire()
{
  try {
    Util::write_file(m_alive_file, "");
  } catch (const core::Error& e) {
    LOG("Failed to create {}: {}", m_alive_file, e.what());
  }
  LOG_RAW("Starting keep-alive thread");
  m_keep_alive_thread = std::thread([=] {
    while (true) {
      std::unique_lock<std::mutex> lock(m_stop_keep_alive_mutex);
      m_stop_keep_alive_condition.wait_for(
        lock, k_keep_alive_interval_ms, [this] { return m_stop_keep_alive; });
      if (m_stop_keep_alive) {
        return;
      }
      util::set_timestamps(m_alive_file);
    }
  });
  LOG_RAW("Started keep-alive thread");
}

void
LongLivedLockFile::on_before_release()
{
  if (m_keep_alive_thread.joinable()) {
    {
      std::unique_lock<std::mutex> lock(m_stop_keep_alive_mutex);
      m_stop_keep_alive = true;
    }
    m_stop_keep_alive_condition.notify_one();
    m_keep_alive_thread.join();

    Util::unlink_tmp(m_alive_file);
  }
}

bool
LongLivedLockFile::on_before_break()
{
  return Util::unlink_tmp(m_alive_file);
}

std::optional<double>
LongLivedLockFile::get_last_lock_update()
{
  if (const auto stat = Stat::stat(m_alive_file); stat) {
    return time_from_stat(stat);
  } else {
    return std::nullopt;
  }
}

#endif

LockFileGuard::LockFileGuard(LockFile& lock_file, Mode mode)
  : m_lock_file(lock_file)
{
  if (mode == Mode::blocking) {
    lock_file.acquire();
  } else {
    lock_file.try_acquire();
  }
}

LockFileGuard::~LockFileGuard() noexcept
{
  m_lock_file.release();
}

bool
LockFileGuard::acquired() const
{
  return m_lock_file.acquired();
}

} // namespace util
