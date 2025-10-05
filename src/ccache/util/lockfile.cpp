// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include "lockfile.hpp"

#include <ccache/util/assertions.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/error.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/process.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef __CYGWIN__
#  include <fcntl.h>
#endif

#include <algorithm>
#include <random>
#include <sstream>

using namespace std::literals::chrono_literals;

namespace fs = util::filesystem;

const uint32_t k_min_sleep_time_ms = 10;
const uint32_t k_max_sleep_time_ms = 50;
#ifndef _WIN32
const auto k_staleness_limit = 2s;
#endif

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
  }

private:
  std::random_device m_random_device;
  std::default_random_engine m_random_engine;
  std::uniform_int_distribution<int32_t> m_distribution;
};

} // namespace

namespace util {

LockFile::LockFile(const fs::path& path)
  : m_lock_file(util::pstr(path).str() + ".lock"),
#ifndef _WIN32
    m_alive_file(util::pstr(path).str() + ".alive"),
    m_acquired(false)
#else
    m_handle(INVALID_HANDLE_VALUE)
#endif
{
}

LockFile::LockFile(LockFile&& other) noexcept
  : m_lock_file(std::move(other.m_lock_file)),
#ifndef _WIN32
    m_lock_manager(other.m_lock_manager),
    m_alive_file(std::move(other.m_alive_file)),
    m_acquired(other.m_acquired)
#else
    m_handle(other.m_handle)
#endif
{
#ifndef _WIN32
  other.m_lock_manager = nullptr;
  other.m_acquired = false;
#else
  other.m_handle = INVALID_HANDLE_VALUE;
#endif
}

LockFile&
LockFile::operator=(LockFile&& other) noexcept
{
  if (&other != this) {
    m_lock_file = std::move(other.m_lock_file);
#ifndef _WIN32
    m_lock_manager = other.m_lock_manager;
    other.m_lock_manager = nullptr;
    m_alive_file = std::move(other.m_alive_file);
    m_acquired = other.m_acquired;
    other.m_acquired = false;
#else
    m_handle = other.m_handle;
    other.m_handle = INVALID_HANDLE_VALUE;
#endif
  }
  return *this;
}

void
LockFile::make_long_lived(
  [[maybe_unused]] LongLivedLockFileManager& lock_manager)
{
#ifndef _WIN32
  m_lock_manager = &lock_manager;
  if (acquired()) {
    m_lock_manager->register_alive_file(m_alive_file);
  }
#endif
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
  if (m_lock_manager) {
    m_lock_manager->deregister_alive_file(m_alive_file);
  }
  if (auto r = fs::remove(m_alive_file); !r) {
    LOG("Failed to remove {}: {}", m_alive_file, r.error());
  }
  if (auto r = fs::remove(m_lock_file); !r) {
    LOG("Failed to remove {}: {}", m_lock_file, r.error());
  }
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
#ifndef _WIN32
    LOG("Creating {}", m_alive_file);
    const auto result = write_file(m_alive_file, "");
    if (!result) {
      LOG("Failed to write {}: {}", m_alive_file, result.error());
    }
    if (m_lock_manager) {
      m_lock_manager->register_alive_file(m_alive_file);
    }
#endif
  } else {
    LOG("Failed to acquire lock {}", m_lock_file);
  }

  return acquired();
}

#ifndef _WIN32

bool
LockFile::do_acquire(const bool blocking)
{
  std::stringstream ss;
  ss << get_hostname() << '-' << getpid() << '-' << std::this_thread::get_id();
  const auto content_prefix = ss.str();

  TimePoint last_seen_activity = [this] {
    const auto last_lock_update = get_last_lock_update();
    return last_lock_update ? *last_lock_update : util::now();
  }();

  std::string initial_content;
  RandomNumberGenerator sleep_ms_generator(k_min_sleep_time_ms,
                                           k_max_sleep_time_ms);

  while (true) {
    const auto now = util::now();
    const auto my_content =
      FMT("{}-{}.{}", content_prefix, util::sec(now), util::nsec_part(now));
#  ifdef __CYGWIN__
    // Cygwin/MSYS2 does not support allow a symlink to have a nonexistent
    // target, so use plain files instead.
    if (write_file(m_lock_file, my_content, WriteFileMode::exclusive)) {
      // We got the lock.
      return true;
    }
#  else
    if (fs::create_symlink(my_content, m_lock_file)) {
      // We got the lock.
      return true;
    }
#  endif

    int saved_errno = errno;
    if (saved_errno == ENOENT) {
      // Directory doesn't exist?
      if (
#  ifdef __CYGWIN__
        // According to #1416, MSYS2 can in some cases set errno to ENOENT even
        // if the directory exists.
        !fs::exists(m_lock_file.parent_path()) &&
#  endif
        fs::create_directories(m_lock_file.parent_path())) {
        // OK. Retry.
        continue;
      }
    }
    LOG("Could not acquire {}: {}", m_lock_file, strerror(saved_errno));

    if (saved_errno == EPERM) {
      // The file system does not support symbolic links. We have no choice but
      // to grant the lock anyway.
      return true;
    }

    if (saved_errno != EEXIST) {
      // Directory doesn't exist or isn't writable?
      return false;
    }
#  ifdef __CYGWIN__
    auto file_content = read_file<std::string>(m_lock_file);
    if (!file_content) {
      // The lock file was removed after the write_file() call above, so retry
      // acquiring it.
      continue;
    }
    auto content = *file_content;
#  else
    auto content_path = fs::read_symlink(m_lock_file);
    if (!content_path) {
      if (content_path.error() == std::errc::no_such_file_or_directory) {
        // The symlink was removed after the symlink() call above, so retry
        // acquiring it.
        continue;
      } else {
        LOG("Could not read symlink {}: {}",
            m_lock_file,
            content_path.error().message());
        return false;
      }
    }
    auto content = content_path->string();
#  endif

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
    if (last_lock_update && *last_lock_update > last_seen_activity) {
      if (!blocking) {
        return false;
      }
      last_seen_activity = *last_lock_update;
    }

    const auto inactive_duration = util::now() - last_seen_activity;

    if (inactive_duration < k_staleness_limit) {
      LOG("Lock {} held by another process active {}.{:03} seconds ago",
          m_lock_file,
          util::sec(inactive_duration),
          util::nsec_part(inactive_duration) / 1'000'000);
    } else if (content == initial_content) {
      // The lock seems to be stale -- break it and try again.
      LOG("Breaking {} since it has been inactive for {}.{:03} seconds",
          m_lock_file,
          util::sec(inactive_duration),
          util::nsec_part(inactive_duration) / 1'000'000);
      if (auto r = fs::remove(m_alive_file);
          !r && r.error() != std::errc::no_such_file_or_directory) {
        return false;
      }
      if (auto r = fs::remove(m_lock_file);
          !r && r.error() != std::errc::no_such_file_or_directory) {
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

std::optional<TimePoint>
LockFile::get_last_lock_update()
{
  if (DirEntry entry(m_alive_file); entry) {
    return entry.mtime();
  } else {
    return std::nullopt;
  }
}

#else // !_WIN32

void*
LockFile::do_acquire(const bool blocking)
{
  void* handle;
  RandomNumberGenerator sleep_ms_generator(k_min_sleep_time_ms,
                                           k_max_sleep_time_ms);

  while (true) {
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE;
    handle = CreateFile(util::pstr(m_lock_file).c_str(),
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
      if (fs::create_directories(m_lock_file.parent_path())) {
        // OK. Retry.
        continue;
      }
    }

    LOG("Could not acquire {}: {} ({})",
        m_lock_file,
        util::win32_error_message(error),
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

} // namespace util
