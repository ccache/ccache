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

#include "Util.hpp"
#include "legacy_util.hpp"
#include "logging.hpp"

#ifdef _WIN32
#  include "win32compat.hpp"
#endif

#include "third_party/fmt/core.h"

namespace {

#ifndef _WIN32

bool
do_acquire_posix(const std::string& lockfile, uint32_t staleness_limit)
{
  const uint32_t max_to_sleep = 10000; // Microseconds.
  uint32_t to_sleep = 1000;            // Microseconds.
  uint32_t slept = 0;                  // Microseconds.
  std::string initial_content;

  while (true) {
    std::string my_content =
      fmt::format("{}:{}:{}", get_hostname(), getpid(), time(nullptr));

    if (symlink(my_content.c_str(), lockfile.c_str()) == 0) {
      // We got the lock.
      return true;
    }

    int saved_errno = errno;
    cc_log("lockfile_acquire: symlink %s: %s",
           lockfile.c_str(),
           strerror(saved_errno));
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
        cc_log("lockfile_acquire: readlink %s: %s",
               lockfile.c_str(),
               strerror(errno));
        return false;
      }
    }

    if (content == my_content) {
      // Lost NFS reply?
      cc_log("lockfile_acquire: symlink %s failed but we got the lock anyway",
             lockfile.c_str());
      return true;
    }

    // A possible improvement here would be to check if the process holding the
    // lock is still alive and break the lock early if it isn't.
    cc_log("lockfile_acquire: lock info for %s: %s",
           lockfile.c_str(),
           content.c_str());

    if (initial_content.empty()) {
      initial_content = content;
    }

    if (slept <= staleness_limit) {
      cc_log("lockfile_acquire: failed to acquire %s; sleeping %u microseconds",
             lockfile.c_str(),
             to_sleep);
      usleep(to_sleep);
      slept += to_sleep;
      to_sleep = std::min(max_to_sleep, 2 * to_sleep);
    } else if (content != initial_content) {
      cc_log("lockfile_acquire: gave up acquiring %s", lockfile.c_str());
      return false;
    } else {
      // The lock seems to be stale -- break it and try again.
      cc_log("lockfile_acquire: breaking %s", lockfile.c_str());
      if (!Util::unlink_tmp(lockfile)) {
        cc_log("Failed to unlink %s: %s", lockfile.c_str(), strerror(errno));
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
                        NULL,          // security attributes
                        CREATE_ALWAYS, // creation disposition
                        flags,         // flags and attributes
                        NULL           // template file
    );
    if (handle != INVALID_HANDLE_VALUE) {
      break;
    }

    DWORD error = GetLastError();
    cc_log("lockfile_acquire: CreateFile %s: %s (%lu)",
           lockfile.c_str(),
           win32_error_message(error).c_str(),
           error);
    if (error == ERROR_PATH_NOT_FOUND) {
      // Directory doesn't exist?
      if (Util::create_dir(Util::dir_name(lockfile)) == 0) {
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
      cc_log("lockfile_acquire: gave up acquiring %s", lockfile.c_str());
      break;
    }

    cc_log("lockfile_acquire: failed to acquire %s; sleeping %u microseconds",
           lockfile.c_str(),
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
    cc_log("Acquired lock %s", m_lockfile.c_str());
  } else {
    cc_log("Failed to acquire lock %s", m_lockfile.c_str());
  }
}

Lockfile::~Lockfile()
{
  if (acquired()) {
    cc_log("Releasing lock %s", m_lockfile.c_str());
#ifndef _WIN32
    if (!Util::unlink_tmp(m_lockfile)) {
      cc_log("Failed to unlink %s: %s", m_lockfile.c_str(), strerror(errno));
    }
#else
    CloseHandle(m_handle);
#endif
  }
}
