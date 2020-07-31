// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "legacy_util.hpp"

#include "Fd.hpp"
#include "exceptions.hpp"
#include "logging.hpp"

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

// Return whether the argument is a full path.
bool
is_full_path(const char* path)
{
#ifdef _WIN32
  if (strchr(path, '\\')) {
    return true;
  }
#endif
  return strchr(path, '/');
}

// Update the modification time of a file in the cache to save it from LRU
// cleanup.
void
update_mtime(const char* path)
{
#ifdef HAVE_UTIMES
  utimes(path, nullptr);
#else
  utime(path, NULL);
#endif
}

// If exit() already has been called, call _exit(), otherwise exit(). This is
// used to avoid calling exit() inside an atexit handler.
void
x_exit(int status)
{
  static bool first_time = true;
  if (first_time) {
    first_time = false;
    exit(status);
  } else {
    _exit(status);
  }
}

// Rename oldpath to newpath (deleting newpath).
int
x_rename(const char* oldpath, const char* newpath)
{
#ifndef _WIN32
  return rename(oldpath, newpath);
#else
  // Windows' rename() refuses to overwrite an existing file.
  // If the function succeeds, the return value is nonzero.
  if (MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING) == 0) {
    DWORD error = GetLastError();
    cc_log("failed to rename %s to %s: %s (%lu)",
           oldpath,
           newpath,
           Win32Util::error_message(error).c_str(),
           error);
    return -1;
  } else {
    return 0;
  }
#endif
}

void
set_cloexec_flag(int fd)
{
#ifndef _WIN32
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
#else
  (void)fd;
#endif
}

double
time_seconds()
{
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#else
  return (double)time(NULL);
#endif
}
