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
#  include "win32compat.hpp"
#endif

#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

// Something went badly wrong!
void
fatal(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  char msg[8192];
  vsnprintf(msg, sizeof(msg), format, ap);
  va_end(ap);

  throw FatalError(msg);
}

// Return a static string with the current hostname.
const char*
get_hostname()
{
  static char hostname[260] = "";

  if (hostname[0]) {
    return hostname;
  }

  if (gethostname(hostname, sizeof(hostname)) != 0) {
    strcpy(hostname, "unknown");
  }
  hostname[sizeof(hostname) - 1] = 0;
  return hostname;
}

// This is like setenv.
void
x_setenv(const char* name, const char* value)
{
#ifdef HAVE_SETENV
  setenv(name, value, true);
#else
  char* string;
  asprintf(&string, "%s=%s", name, value);
  putenv(string);       // Leak to environment.
#endif
}

// This is like unsetenv.
void
x_unsetenv(const char* name)
{
#ifdef HAVE_UNSETENV
  unsetenv(name);
#else
  putenv(strdup(name)); // Leak to environment.
#endif
}

#if !defined(_WIN32) && !defined(HAVE_LOCALTIME_R)
// localtime_r replacement. (Mingw-w64 has an inline localtime_r which is not
// detected by AC_CHECK_FUNCS.)
struct tm*
localtime_r(const time_t* timep, struct tm* result)
{
  struct tm* tm = localtime(timep);
  if (tm) {
    *result = *tm;
    return result;
  } else {
    memset(result, 0, sizeof(*result));
    return NULL;
  }
}
#endif

// Return current user's home directory, or throw FatalError if it can't be
// determined.
const char*
get_home_directory()
{
  const char* p = getenv("HOME");
  if (p) {
    return p;
  }
#ifdef _WIN32
  p = getenv("APPDATA");
  if (p) {
    return p;
  }
#endif
#ifdef HAVE_GETPWUID
  {
    struct passwd* pwd = getpwuid(getuid());
    if (pwd) {
      return pwd->pw_dir;
    }
  }
#endif
  fatal("Could not determine home directory from $HOME or getpwuid(3)");
}

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
           win32_error_message(error).c_str(),
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
