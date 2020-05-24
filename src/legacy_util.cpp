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
copy_fd(int fd_in, int fd_out, bool fd_in_is_file)
{
  ssize_t n;
  char buf[READ_BUFFER_SIZE];
  while ((n = read(fd_in, buf, sizeof(buf))) > 0) {
    if (!write_fd(fd_out, buf, n)) {
      return false;
    }

    if (fd_in_is_file && static_cast<size_t>(n) < sizeof(buf)) {
      break;
    }
  }

  return true;
}

#ifndef HAVE_MKSTEMP
// Cheap and nasty mkstemp replacement.
int
mkstemp(char* name_template)
{
#  ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  endif
  mktemp(name_template);
#  ifdef __GNUC__
#    pragma GCC diagnostic pop
#  endif
  return open(name_template, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
}
#endif

#ifndef _WIN32
static mode_t
get_umask()
{
  static bool mask_retrieved = false;
  static mode_t mask;
  if (!mask_retrieved) {
    mask = umask(0);
    umask(mask);
    mask_retrieved = true;
  }
  return mask;
}
#endif

// Clone a file from src to dest. If via_tmp_file is true, the file is cloned
// to a temporary file and then renamed to dest.
bool
clone_file(const char* src, const char* dest, bool via_tmp_file)
{
#ifdef FILE_CLONING_SUPPORTED

  bool result;

#  if defined(__linux__)
  int src_fd = open(src, O_RDONLY);
  if (src_fd == -1) {
    return false;
  }

  int dest_fd;
  char* tmp_file = nullptr;
  if (via_tmp_file) {
    tmp_file = x_strdup(dest);
    dest_fd = create_tmp_fd(&tmp_file);
  } else {
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_BINARY, 0666);
    if (dest_fd == -1) {
      close(dest_fd);
      close(src_fd);
      return false;
    }
  }

  int saved_errno = 0;
  if (ioctl(dest_fd, FICLONE, src_fd) == 0) {
    result = true;
  } else {
    result = false;
    saved_errno = errno;
  }

  close(dest_fd);
  close(src_fd);

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

  int src_fd = open(src, O_RDONLY);
  if (src_fd == -1) {
    return false;
  }

  int dest_fd;
  char* tmp_file = nullptr;
  if (via_tmp_file) {
    tmp_file = x_strdup(dest);
    dest_fd = create_tmp_fd(&tmp_file);
  } else {
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_BINARY, 0666);
    if (dest_fd == -1) {
      close(dest_fd);
      close(src_fd);
      return false;
    }
  }

  if (copy_fd(src_fd, dest_fd, true)) {
    result = true;
  }

  close(dest_fd);
  close(src_fd);

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

// Return a string to be passed to mkstemp to create a temporary file. Also
// tries to cope with NFS by adding the local hostname.
const char*
tmp_string()
{
  static char* ret;
  if (!ret) {
    ret = format("%s.%u.XXXXXX", get_hostname(), (unsigned)getpid());
  }
  return ret;
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

// Construct a hexadecimal string representing binary data. The buffer must
// hold at least 2 * size + 1 bytes.
void
format_hex(const uint8_t* data, size_t size, char* buffer)
{
  for (size_t i = 0; i < size; i++) {
    sprintf(&buffer[i * 2], "%02x", (unsigned)data[i]);
  }
  buffer[2 * size] = '\0';
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

// This is like malloc() but dies if the malloc fails.
void*
x_malloc(size_t size)
{
  if (size == 0) {
    // malloc() may return NULL if size is zero, so always do this to make sure
    // that the code handles it regardless of platform.
    return nullptr;
  }
  void* ret = malloc(size);
  if (!ret) {
    fatal("x_malloc: Could not allocate %lu bytes", (unsigned long)size);
  }
  return ret;
}

// This is like realloc() but dies if the malloc fails.
void*
x_realloc(void* ptr, size_t size)
{
  if (!ptr) {
    return x_malloc(size);
  }
  void* p2 = realloc(ptr, size);
  if (!p2) {
    fatal("x_realloc: Could not allocate %lu bytes", (unsigned long)size);
  }
  return p2;
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

// Return the dir name of a file - caller frees.
char*
x_dirname(const char* path)
{
  char* s = x_strdup(path);
  char* p = strrchr(s, '/');
#ifdef _WIN32
  char* p2 = strrchr(s, '\\');
  if (!p || (p2 && p < p2)) {
    p = p2;
  }
#endif
  if (!p) {
    free(s);
    s = x_strdup(".");
  } else if (p == s) {
    *(p + 1) = 0;
  } else {
    *p = 0;
  }
  return s;
}

// Return the file extension (including the dot) of a path as a pointer into
// path. If path has no file extension, the empty string and the end of path is
// returned.
const char*
get_extension(const char* path)
{
  size_t len = strlen(path);
  for (const char* p = &path[len - 1]; p >= path; --p) {
    if (*p == '.') {
      return p;
    }
    if (*p == '/') {
      break;
    }
  }
  return &path[len];
}

// Format a size as a human-readable string. Caller frees.
char*
format_human_readable_size(uint64_t v)
{
  char* s;
  if (v >= 1000 * 1000 * 1000) {
    s = format("%.1f GB", v / ((double)(1000 * 1000 * 1000)));
  } else {
    s = format("%.1f MB", v / ((double)(1000 * 1000)));
  }
  return s;
}

// Format a size as a parsable string. Caller frees.
char*
format_parsable_size_with_suffix(uint64_t size)
{
  char* s;
  if (size >= 1000 * 1000 * 1000) {
    s = format("%.1fG", size / ((double)(1000 * 1000 * 1000)));
  } else if (size >= 1000 * 1000) {
    s = format("%.1fM", size / ((double)(1000 * 1000)));
  } else {
    s = format("%u", (unsigned)size);
  }
  return s;
}

// Parse a "size value", i.e. a string that can end in k, M, G, T (10-based
// suffixes) or Ki, Mi, Gi, Ti (2-based suffixes). For backward compatibility,
// K is also recognized as a synonym of k.
bool
parse_size_with_suffix(const char* str, uint64_t* size)
{
  errno = 0;

  char* p;
  double x = strtod(str, &p);
  if (errno != 0 || x < 0 || p == str || *str == '\0') {
    return false;
  }

  while (isspace(*p)) {
    ++p;
  }

  if (*p != '\0') {
    unsigned multiplier = *(p + 1) == 'i' ? 1024 : 1000;
    switch (*p) {
    case 'T':
      x *= multiplier;
    // Fallthrough.
    case 'G':
      x *= multiplier;
    // Fallthrough.
    case 'M':
      x *= multiplier;
    // Fallthrough.
    case 'K':
    case 'k':
      x *= multiplier;
      break;
    default:
      return false;
    }
  } else {
    // Default suffix: G.
    x *= 1000 * 1000 * 1000;
  }
  *size = (uint64_t)x;
  return true;
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

// Create an empty temporary file. *fname will be reallocated and set to the
// resulting filename. Returns an open file descriptor to the file.
int
create_tmp_fd(char** fname)
{
  char* tmpl = format("%s.%s", *fname, tmp_string());
  int fd = mkstemp(tmpl);
  if (fd == -1 && errno == ENOENT) {
    if (!Util::create_dir(Util::dir_name(*fname))) {
      fatal("Failed to create directory %s: %s",
            x_dirname(*fname),
            strerror(errno));
    }
    reformat(&tmpl, "%s.%s", *fname, tmp_string());
    fd = mkstemp(tmpl);
  }
  if (fd == -1) {
    fatal(
      "Failed to create temporary file for %s: %s", *fname, strerror(errno));
  }
  set_cloexec_flag(fd);

#ifndef _WIN32
  fchmod(fd, 0666 & ~get_umask());
#endif

  free(*fname);
  *fname = tmpl;
  return fd;
}

// Create an empty temporary file. *fname will be reallocated and set to the
// resulting filename. Returns an open FILE*.
FILE*
create_tmp_file(char** fname, const char* mode)
{
  FILE* file = fdopen(create_tmp_fd(fname), mode);
  if (!file) {
    fatal("Failed to create file %s: %s", *fname, strerror(errno));
  }
  return file;
}

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
  if (strchr(path, '/')) {
    return true;
  }
#ifdef _WIN32
  if (strchr(path, '\\')) {
    return true;
  }
#endif
  return false;
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

// Reads the content of a file. Size hint 0 means no hint. Returns true on
// success, otherwise false.
bool
read_file(const char* path, size_t size_hint, char** data, size_t* size)
{
  if (size_hint == 0) {
    size_hint = Stat::stat(path, Stat::OnError::log).size();
  }
  // +1 to be able to detect EOF in the first read call
  size_hint = (size_hint < 1024) ? 1024 : size_hint + 1;

  int fd = open(path, O_RDONLY | O_BINARY);
  if (fd == -1) {
    return false;
  }
  size_t allocated = size_hint;
  *data = static_cast<char*>(x_malloc(allocated));
  ssize_t ret;
  size_t pos = 0;
  while (true) {
    if (pos > allocated / 2) {
      allocated *= 2;
      *data = static_cast<char*>(x_realloc(*data, allocated));
    }
    const size_t max_read = allocated - pos;
    ret = read(fd, *data + pos, max_read);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
      break;
    }
    if (ret > 0) {
      pos += ret;
      if (static_cast<size_t>(ret) < max_read) {
        break;
      }
    }
  }
  close(fd);
  if (ret == -1) {
    cc_log("Failed reading %s", path);
    free(*data);
    *data = nullptr;
    return false;
  }

  *size = pos;
  return true;
}

// Return the content (with NUL termination) of a text file, or NULL on error.
// Caller frees. Size hint 0 means no hint.
char*
read_text_file(const char* path, size_t size_hint)
{
  size_t size;
  char* data;
  if (read_file(path, size_hint, &data, &size)) {
    data = static_cast<char*>(x_realloc(data, size + 1));
    data[size] = '\0';
    return data;
  } else {
    return nullptr;
  }
}

static bool
expand_variable(const char** str, char** result, char** errmsg)
{
  assert(**str == '$');

  bool curly;
  const char* p = *str + 1;
  if (*p == '{') {
    curly = true;
    ++p;
  } else {
    curly = false;
  }

  const char* q = p;
  while (isalnum(*q) || *q == '_') {
    ++q;
  }
  if (curly) {
    if (*q != '}') {
      *errmsg = format("syntax error: missing '}' after \"%s\"", p);
      return false;
    }
  }

  if (q == p) {
    // Special case: don't consider a single $ the start of a variable.
    reformat(result, "%s$", *result);
    return true;
  }

  char* name = x_strndup(p, q - p);
  const char* value = getenv(name);
  if (!value) {
    *errmsg = format("environment variable \"%s\" not set", name);
    free(name);
    return false;
  }
  reformat(result, "%s%s", *result, value);
  if (!curly) {
    --q;
  }
  *str = q;
  free(name);
  return true;
}

// Substitute all instances of $VAR or ${VAR}, where VAR is an environment
// variable, in a string. Caller frees. If one of the environment variables
// doesn't exist, NULL will be returned and *errmsg will be an appropriate
// error message (caller frees).
char*
subst_env_in_string(const char* str, char** errmsg)
{
  assert(errmsg);
  *errmsg = nullptr;

  char* result = x_strdup("");
  const char* p = str; // Interval start.
  const char* q = str; // Interval end.
  for (; *q; ++q) {
    if (*q == '$') {
      reformat(&result, "%s%.*s", result, (int)(q - p), p);
      if (!expand_variable(&q, &result, errmsg)) {
        free(result);
        return nullptr;
      }
      p = q + 1;
    }
  }
  reformat(&result, "%s%.*s", result, (int)(q - p), p);
  return result;
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
