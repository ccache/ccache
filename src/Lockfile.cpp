// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "Lockfile.hpp"

#include "Logging.hpp"
#include "Util.hpp"
#include "fmtmacros.hpp"

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#include "third_party/fmt/core.h"

#include <algorithm>
#include <sstream>
#include <thread>

namespace {

#ifndef _WIN32

bool
do_acquire_posix(const std::string& lockfile, uint32_t staleness_limit)
{
  const uint32_t max_to_sleep = 10000; // Microseconds.
  uint32_t to_sleep = 1000;            // Microseconds.
  uint32_t slept = 0;                  // Microseconds.
  std::string initial_content;

  std::stringstream ss;
  ss << Util::get_hostname() << ':' << getpid() << ':'
     << std::this_thread::get_id();
  const auto content_prefix = ss.str();

  while (true) {
    auto my_content = FMT("{}:{}", content_prefix, time(nullptr));

    if (symlink(my_content.c_str(), lockfile.c_str()) == 0) {
      // We got the lock.
      return true;
    }

    int saved_errno = errno;
    LOG("lockfile_acquire: symlink {}: {}", lockfile, strerror(saved_errno));
    if (saved_errno == ENOENT) {
      // Directory doesn't exist?
      if (Util::create_dir(Util::dir_name(lockfile))) {
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

    std::string content = Util::read_link(lockfile);
    if (content.empty()) {
      if (errno == ENOENT) {
        // The symlink was removed after the symlink() call above, so retry
        // acquiring it.
        continue;
      } else {
        LOG("lockfile_acquire: readlink {}: {}", lockfile, strerror(errno));
        return false;
      }
    }

    if (content == my_content) {
      // Lost NFS reply?
      LOG("lockfile_acquire: symlink {} failed but we got the lock anyway",
          lockfile);
      return true;
    }

    // A possible improvement here would be to check if the process holding the
    // lock is still alive and break the lock early if it isn't.
    LOG("lockfile_acquire: lock info for {}: {}", lockfile, content);

    if (initial_content.empty()) {
      initial_content = content;
    }

    if (slept <= staleness_limit) {
      LOG("lockfile_acquire: failed to acquire {}; sleeping {} microseconds",
          lockfile,
          to_sleep);
      usleep(to_sleep);
      slept += to_sleep;
      to_sleep = std::min(max_to_sleep, 2 * to_sleep);
    } else if (content != initial_content) {
      LOG("lockfile_acquire: gave up acquiring {}", lockfile);
      return false;
    } else {
      // The lock seems to be stale -- break it and try again.
      LOG("lockfile_acquire: breaking {}", lockfile);
      if (!Util::unlink_tmp(lockfile)) {
        LOG("Failed to unlink {}: {}", lockfile, strerror(errno));
        return false;
      }
      to_sleep = 1000;
      slept = 0;
      initial_content.clear();
    }
  }
}

#else // !_WIN32

HANDLE
do_acquire_win32(const std::string& lockfile, uint32_t staleness_limit)
{
  unsigned to_sleep = 1000;      // Microseconds.
  unsigned max_to_sleep = 10000; // Microseconds.
  unsigned slept = 0;            // Microseconds.
  HANDLE handle;

  while (true) {
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE;
    handle = CreateFile(lockfile.c_str(),
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
    LOG("lockfile_acquire: CreateFile {}: {} ({})",
        lockfile,
        Win32Util::error_message(error),
        error);
    if (error == ERROR_PATH_NOT_FOUND) {
      // Directory doesn't exist?
      if (Util::create_dir(Util::dir_name(lockfile))) {
        // OK. Retry.
        continue;
      }
    }

    // ERROR_SHARING_VIOLATION: lock already held.
    // ERROR_ACCESS_DENIED: maybe pending delete.
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) {
      // Fatal error, give up.
      break;
    }

    if (slept > staleness_limit) {
      LOG("lockfile_acquire: gave up acquiring {}", lockfile);
      break;
    }

    LOG("lockfile_acquire: failed to acquire {}; sleeping {} microseconds",
        lockfile,
        to_sleep);
    usleep(to_sleep);
    slept += to_sleep;
    to_sleep = std::min(max_to_sleep, 2 * to_sleep);
  }

  return handle;
}

#endif // !_WIN32

} // namespace

Lockfile::Lockfile(const std::string& path, uint32_t staleness_limit)
  : m_lockfile(path + ".lock")
{
#ifndef _WIN32
  m_acquired = do_acquire_posix(m_lockfile, staleness_limit);
#else
  m_handle = do_acquire_win32(m_lockfile, staleness_limit);
#endif
  if (acquired()) {
    LOG("Acquired lock {}", m_lockfile);
  } else {
    LOG("Failed to acquire lock {}", m_lockfile);
  }
}

Lockfile::~Lockfile()
{
  if (acquired()) {
    LOG("Releasing lock {}", m_lockfile);
#ifndef _WIN32
    if (!Util::unlink_tmp(m_lockfile)) {
      LOG("Failed to unlink {}: {}", m_lockfile, strerror(errno));
    }
#else
    CloseHandle(m_handle);
#endif
  }
}
