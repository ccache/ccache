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
#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "exceptions.hpp"
#include "logging.hpp"

#ifdef _WIN32
#  include "win32compat.hpp"
#endif

#include "third_party/fmt/core.h"

#include <string>

#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

#ifdef __linux__
#  ifdef HAVE_SYS_IOCTL_H
#    include <sys/ioctl.h>
#  endif
#  ifdef HAVE_LINUX_FS_H
#    include <linux/fs.h>
#    ifndef FICLONE
#      define FICLONE _IOW(0x94, 9, int)
#    endif
#    define FILE_CLONING_SUPPORTED 1
#  endif
#endif

#ifdef __APPLE__
#  ifdef HAVE_SYS_CLONEFILE_H
#    include <sys/clonefile.h>
#    define FILE_CLONING_SUPPORTED 1
#  endif
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

bool
write_fd(int fd, const void* buf, size_t size)
{
  ssize_t written = 0;
  do {
    ssize_t count =
      write(fd, static_cast<const uint8_t*>(buf) + written, size - written);
    if (count == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        return false;
      }
    } else {
      written += count;
    }
  } while (static_cast<size_t>(written) < size);

  return true;
}

// Copy all data from fd_in to fd_out.
bool
copy_fd(int fd_in, int fd_out)
{
  ssize_t n;
  char buf[READ_BUFFER_SIZE];
  while ((n = read(fd_in, buf, sizeof(buf))) != 0) {
    if (n == -1 && errno != EINTR) {
      break;
    }
    if (n > 0 && !write_fd(fd_out, buf, n)) {
      return false;
    }
  }

  return true;
}

// Clone a file from src to dest. If via_tmp_file is true, the file is cloned
// to a temporary file and then renamed to dest.
bool
clone_file(const char* src, const char* dest, bool via_tmp_file)
{
#ifdef FILE_CLONING_SUPPORTED
  bool result;

#  if defined(__linux__)
  Fd src_fd(open(src, O_RDONLY));
  if (!src_fd) {
    return false;
  }

  Fd dest_fd;
  char* tmp_file = nullptr;
  if (via_tmp_file) {
    TemporaryFile temp_file(dest);
    dest_fd = std::move(temp_file.fd);
    tmp_file = x_strdup(temp_file.path.c_str());
  } else {
    dest_fd = Fd(open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      return false;
    }
  }

  int saved_errno = 0;
  if (ioctl(*dest_fd, FICLONE, *src_fd) == 0) {
    result = true;
  } else {
    result = false;
    saved_errno = errno;
  }

  dest_fd.close();
  src_fd.close();

  if (via_tmp_file) {
    if (x_rename(tmp_file, dest) != 0) {
      result = false;
    }
    free(tmp_file);
  }

  errno = saved_errno;
#  elif defined(__APPLE__)
  (void)via_tmp_file;
  result = clonefile(src, dest, CLONE_NOOWNERCOPY) == 0;
#  endif

  return result;

#else // FILE_CLONING_SUPPORTED

  (void)src;
  (void)dest;
  (void)via_tmp_file;
  errno = EOPNOTSUPP;
  return false;

#endif // FILE_CLONING_SUPPORTED
}

// Copy a file from src to dest. If via_tmp_file is true, the file is copied to
// a temporary file and then renamed to dest.
bool
copy_file(const char* src, const char* dest, bool via_tmp_file)
{
  bool result = false;

  Fd src_fd(open(src, O_RDONLY));
  if (!src_fd) {
    return false;
  }

  Fd dest_fd;
  char* tmp_file = nullptr;
  if (via_tmp_file) {
    TemporaryFile temp_file(dest);
    dest_fd = std::move(temp_file.fd);
    tmp_file = x_strdup(temp_file.path.c_str());
  } else {
    dest_fd = Fd(open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      return false;
    }
  }

  if (copy_fd(*src_fd, *dest_fd)) {
    result = true;
  }

  dest_fd.close();
  src_fd.close();

  if (via_tmp_file) {
    if (x_rename(tmp_file, dest) != 0) {
      result = false;
    }
    free(tmp_file);
  }

  return result;
}

// Run copy_file() and, if successful, delete the source file.
bool
move_file(const char* src, const char* dest)
{
  bool ok = copy_file(src, dest, false);
  if (ok) {
    Util::unlink_safe(src);
  }
  return ok;
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

// Construct a string according to a format. Caller frees.
char*
format(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);

  char* ptr = nullptr;
  if (vasprintf(&ptr, format, ap) == -1) {
    fatal("Out of memory in format");
  }
  va_end(ap);

  if (!*ptr) {
    fatal("Internal error in format");
  }
  return ptr;
}

// This is like strdup() but dies if the malloc fails.
char*
x_strdup(const char* s)
{
  char* ret = strdup(s);
  if (!ret) {
    fatal("Out of memory in x_strdup");
  }
  return ret;
}

// This is like strndup() but dies if the malloc fails.
char*
x_strndup(const char* s, size_t n)
{
#ifndef HAVE_STRNDUP
  if (!s) {
    return NULL;
  }
  size_t m = 0;
  while (m < n && s[m]) {
    m++;
  }
  char* ret = static_cast<char*>(malloc(m + 1));
  if (ret) {
    memcpy(ret, s, m);
    ret[m] = '\0';
  }
#else
  char* ret = strndup(s, n);
#endif
  if (!ret) {
    fatal("x_strndup: Could not allocate %lu bytes", (unsigned long)n);
  }
  return ret;
}

// This is like setenv.
void
x_setenv(const char* name, const char* value)
{
#ifdef HAVE_SETENV
  setenv(name, value, true);
#else
  putenv(format("%s=%s", name, value)); // Leak to environment.
#endif
}

// This is like unsetenv.
void
x_unsetenv(const char* name)
{
#ifdef HAVE_UNSETENV
  unsetenv(name);
#else
  putenv(x_strdup(name));               // Leak to environment.
#endif
}

// Construct a string according to the format and store it in *ptr. The
// original *ptr is then freed.
void
reformat(char** ptr, const char* format, ...)
{
  char* saved = *ptr;
  *ptr = nullptr;

  va_list ap;
  va_start(ap, format);
  if (vasprintf(ptr, format, ap) == -1) {
    fatal("Out of memory in reformat");
  }
  va_end(ap);

  if (saved) {
    free(saved);
  }
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

// Check whether s1 and s2 have the same executable name.
bool
same_executable_name(const char* s1, const char* s2)
{
#ifdef _WIN32
  bool eq = strcasecmp(s1, s2) == 0;
  if (!eq) {
    char* tmp = format("%s.exe", s2);
    eq = strcasecmp(s1, tmp) == 0;
    free(tmp);
  }
  return eq;
#else
  return str_eq(s1, s2);
#endif
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
