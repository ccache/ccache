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

#include "Config.hpp"
#include "Error.hpp"
#include "Util.hpp"
#include "execute.hpp"

#include "third_party/fmt/core.h"

#include <string>

#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
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

#ifdef _WIN32
#  include <psapi.h>
#  include <sys/locking.h>
#  include <tchar.h>
#endif

// Destination for g_config.log_file.
static FILE* logfile;

// Whether to use syslog() instead.
static bool use_syslog;

// Buffer used for logs in debug mode.
static char* debug_log_buffer;

// Allocated debug_log_buffer size.
static size_t debug_log_buffer_capacity;

// The amount of log data stored in debug_log_buffer.
static size_t debug_log_size;

#define DEBUG_LOG_BUFFER_MARGIN 1024

static bool
init_log(void)
{
  if (debug_log_buffer || logfile || use_syslog) {
    return true;
  }
  if (g_config.debug()) {
    debug_log_buffer_capacity = DEBUG_LOG_BUFFER_MARGIN;
    debug_log_buffer = static_cast<char*>(x_malloc(debug_log_buffer_capacity));
    debug_log_size = 0;
  }
  if (g_config.log_file().empty()) {
    return g_config.debug();
  }
#ifdef HAVE_SYSLOG
  if (g_config.log_file() == "syslog") {
    use_syslog = true;
    openlog("ccache", LOG_PID, LOG_USER);
    return true;
  }
#endif
  logfile = fopen(g_config.log_file().c_str(), "a");
  if (logfile) {
#ifndef _WIN32
    set_cloexec_flag(fileno(logfile));
#endif
    return true;
  } else {
    return false;
  }
}

static void
append_to_debug_log(const char* s, size_t len)
{
  assert(debug_log_buffer);
  if (debug_log_size + len + 1 > debug_log_buffer_capacity) {
    debug_log_buffer_capacity += len + 1 + DEBUG_LOG_BUFFER_MARGIN;
    debug_log_buffer = static_cast<char*>(
      x_realloc(debug_log_buffer, debug_log_buffer_capacity));
  }
  memcpy(debug_log_buffer + debug_log_size, s, len);
  debug_log_size += len;
}

static void
log_prefix(bool log_updated_time)
{
  static char prefix[200];
#ifdef HAVE_GETTIMEOFDAY
  if (log_updated_time) {
    char timestamp[100];
    struct tm tm;
    struct timeval tv;
    gettimeofday(&tv, NULL);
#  ifdef __MINGW64_VERSION_MAJOR
    localtime_r((time_t*)&tv.tv_sec, &tm);
#  else
    localtime_r(&tv.tv_sec, &tm);
#  endif
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(prefix,
             sizeof(prefix),
             "[%s.%06d %-5d] ",
             timestamp,
             (int)tv.tv_usec,
             (int)getpid());
  }
#else
  snprintf(prefix, sizeof(prefix), "[%-5d] ", (int)getpid());
#endif
  if (logfile) {
    fputs(prefix, logfile);
  }
#ifdef HAVE_SYSLOG
  if (use_syslog) {
    // prefix information will be added by syslog
  }
#endif
  if (debug_log_buffer) {
    append_to_debug_log(prefix, strlen(prefix));
  }
}

static long
path_max(const char* path)
{
#ifdef PATH_MAX
  (void)path;
  return PATH_MAX;
#elif defined(MAXPATHLEN)
  (void)path;
  return MAXPATHLEN;
#elif defined(_PC_PATH_MAX)
  long maxlen = pathconf(path, _PC_PATH_MAX);
  return maxlen >= 4096 ? maxlen : 4096;
#endif
}

static void warn_log_fail(void) ATTR_NORETURN;

// Warn about failure writing to the log file and then exit.
static void
warn_log_fail(void)
{
  // Note: Can't call fatal() since that would lead to recursion.
  fprintf(stderr,
          "ccache: error: Failed to write to %s: %s\n",
          g_config.log_file().c_str(),
          strerror(errno));
  x_exit(EXIT_FAILURE);
}

static void
vlog(const char* format, va_list ap, bool log_updated_time)
{
  if (!init_log()) {
    return;
  }

  va_list aq;
  va_copy(aq, ap);
  log_prefix(log_updated_time);
  if (logfile) {
    int rc1 = vfprintf(logfile, format, ap);
    int rc2 = fprintf(logfile, "\n");
    if (rc1 < 0 || rc2 < 0) {
      warn_log_fail();
    }
  }
#ifdef HAVE_SYSLOG
  if (use_syslog) {
    vsyslog(LOG_DEBUG, format, ap);
  }
#endif
  if (debug_log_buffer) {
    char buf[8192];
    int len = vsnprintf(buf, sizeof(buf), format, aq);
    if (len >= 0) {
      append_to_debug_log(buf, std::min((size_t)len, sizeof(buf) - 1));
      append_to_debug_log("\n", 1);
    }
  }
  va_end(aq);
}

// Write a message to the log file (adding a newline) and flush.
void
cc_log(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  vlog(format, ap, true);
  va_end(ap);
  if (logfile) {
    fflush(logfile);
  }
}

// Write a message to the log file (adding a newline) without flushing and with
// a reused timestamp.
void
cc_bulklog(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  vlog(format, ap, false);
  va_end(ap);
}

// Log an executed command to the CCACHE_LOGFILE location.
void
cc_log_argv(const char* prefix, char** argv)
{
  if (!init_log()) {
    return;
  }

  log_prefix(true);
  if (logfile) {
    fputs(prefix, logfile);
    print_command(logfile, argv);
    int rc = fflush(logfile);
    if (rc) {
      warn_log_fail();
    }
  }
#ifdef HAVE_SYSLOG
  if (use_syslog) {
    char* s = format_command(argv);
    syslog(LOG_DEBUG, "%s", s);
    free(s);
  }
#endif
  if (debug_log_buffer) {
    append_to_debug_log(prefix, strlen(prefix));
    char* s = format_command(argv);
    append_to_debug_log(s, strlen(s));
    free(s);
  }
}

// Copy the current log memory buffer to an output file.
void
cc_dump_debug_log_buffer(const char* path)
{
  FILE* file = fopen(path, "w");
  if (file) {
    (void)fwrite(debug_log_buffer, 1, debug_log_size, file);
    fclose(file);
  } else {
    cc_log("Failed to open %s: %s", path, strerror(errno));
  }
}

// Something went badly wrong!
void
fatal(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  char msg[8192];
  vsnprintf(msg, sizeof(msg), format, ap);
  va_end(ap);

  cc_log("FATAL: %s", msg);
  fprintf(stderr, "ccache: error: %s\n", msg);

  x_exit(1);
}

// Copy all data from fd_in to fd_out.
bool
copy_fd(int fd_in, int fd_out)
{
  int n;
  char buf[READ_BUFFER_SIZE];
  while ((n = read(fd_in, buf, sizeof(buf))) > 0) {
    ssize_t written = 0;
    do {
      ssize_t count = write(fd_out, buf + written, n - written);
      if (count == -1) {
        if (errno != EAGAIN && errno != EINTR) {
          return false;
        }
      } else {
        written += count;
      }
    } while (written < n);
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
get_umask(void)
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
  char* tmp_file = NULL;
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
    x_try_unlink(dest);
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
  char* tmp_file = NULL;
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

  if (copy_fd(src_fd, dest_fd)) {
    result = true;
  }

  close(dest_fd);
  close(src_fd);

  if (via_tmp_file) {
    x_try_unlink(dest);
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
    x_unlink(src);
  }
  return ok;
}

// Return a static string with the current hostname.
const char*
get_hostname(void)
{
  static char hostname[260] = "";

  if (hostname[0]) {
    return hostname;
  }

  strcpy(hostname, "unknown");
#if HAVE_GETHOSTNAME
  gethostname(hostname, sizeof(hostname) - 1);
#elif defined(_WIN32)
  const char* computer_name = getenv("COMPUTERNAME");
  if (computer_name) {
    snprintf(hostname, sizeof(hostname), "%s", computer_name);
    return hostname;
  }

  WORD w_version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;
  int err = WSAStartup(w_version_requested, &wsa_data);
  if (err != 0) {
    // Tell the user that we could not find a usable Winsock DLL.
    cc_log("WSAStartup failed with error: %d", err);
    return hostname;
  }

  if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
    // Tell the user that we could not find a usable WinSock DLL.
    cc_log("Could not find a usable version of Winsock.dll");
    WSACleanup();
    return hostname;
  }

  int result = gethostname(hostname, sizeof(hostname) - 1);
  if (result != 0) {
    LPVOID lp_msg_buf;
    DWORD dw = WSAGetLastError();

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  dw,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&lp_msg_buf,
                  0,
                  NULL);

    LPVOID lp_display_buf = (LPVOID)LocalAlloc(
      LMEM_ZEROINIT,
      (lstrlen((LPCTSTR)lp_msg_buf) + lstrlen((LPCTSTR)__FILE__) + 200)
        * sizeof(TCHAR));
    _snprintf((LPTSTR)lp_display_buf,
              LocalSize(lp_display_buf) / sizeof(TCHAR),
              TEXT("%s failed with error %lu: %s"),
              __FILE__,
              dw,
              (const char*)lp_msg_buf);

    cc_log("can't get hostname OS returned error: %s", (char*)lp_display_buf);

    LocalFree(lp_msg_buf);
    LocalFree(lp_display_buf);
  }
  WSACleanup();
#endif

  hostname[sizeof(hostname) - 1] = 0;
  return hostname;
}

// Return a string to be passed to mkstemp to create a temporary file. Also
// tries to cope with NFS by adding the local hostname.
const char*
tmp_string(void)
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

  char* ptr = NULL;
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
    return NULL;
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
  *ptr = NULL;

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

#if !defined(HAVE_REALPATH) && defined(_WIN32)                                 \
  && !defined(HAVE_GETFINALPATHNAMEBYHANDLEW)
static BOOL
GetFileNameFromHandle(HANDLE file_handle, TCHAR* filename, WORD cch_filename)
{
  BOOL success = FALSE;

  // Get the file size.
  DWORD file_size_hi = 0;
  DWORD file_size_lo = GetFileSize(file_handle, &file_size_hi);
  if (file_size_lo == 0 && file_size_hi == 0) {
    // Cannot map a file with a length of zero.
    return FALSE;
  }

  // Create a file mapping object.
  HANDLE file_map =
    CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 1, NULL);
  if (!file_map) {
    return FALSE;
  }

  // Create a file mapping to get the file name.
  void* mem = MapViewOfFile(file_map, FILE_MAP_READ, 0, 0, 1);
  if (mem) {
    if (GetMappedFileName(GetCurrentProcess(), mem, filename, cch_filename)) {
      // Translate path with device name to drive letters.
      TCHAR temp[512];
      temp[0] = '\0';

      if (GetLogicalDriveStrings(512 - 1, temp)) {
        TCHAR name[MAX_PATH];
        TCHAR drive[3] = TEXT(" :");
        BOOL found = FALSE;
        TCHAR* p = temp;

        do {
          // Copy the drive letter to the template string.
          *drive = *p;

          // Look up each device name.
          if (QueryDosDevice(drive, name, MAX_PATH)) {
            size_t name_len = _tcslen(name);
            if (name_len < MAX_PATH) {
              found = _tcsnicmp(filename, name, name_len) == 0
                      && *(filename + name_len) == _T('\\');
              if (found) {
                // Reconstruct filename using temp_file and replace device path
                // with DOS path.
                TCHAR temp_file[MAX_PATH];
                _sntprintf(temp_file,
                           MAX_PATH - 1,
                           TEXT("%s%s"),
                           drive,
                           filename + name_len);
                strcpy(filename, temp_file);
              }
            }
          }

          // Go to the next NULL character.
          while (*p++) {
            // Do nothing.
          }
        } while (!found && *p); // End of string.
      }
    }
    success = TRUE;
    UnmapViewOfFile(mem);
  }

  CloseHandle(file_map);
  return success;
}
#endif

// A sane realpath() function, trying to cope with stupid path limits and a
// broken API. Caller frees.
char*
x_realpath(const char* path)
{
  long maxlen = path_max(path);
  char* ret = static_cast<char*>(x_malloc(maxlen));
  char* p;

#if HAVE_REALPATH
  p = realpath(path, ret);
#elif defined(_WIN32)
  if (path[0] == '/') {
    path++; // Skip leading slash.
  }
  HANDLE path_handle = CreateFile(path,
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
  if (INVALID_HANDLE_VALUE != path_handle) {
#  ifdef HAVE_GETFINALPATHNAMEBYHANDLEW
    GetFinalPathNameByHandle(path_handle, ret, maxlen, FILE_NAME_NORMALIZED);
#  else
    GetFileNameFromHandle(path_handle, ret, maxlen);
#  endif
    CloseHandle(path_handle);
    p = ret + 4; // Strip \\?\ from the file name.
  } else {
    snprintf(ret, maxlen, "%s", path);
    p = ret;
  }
#else
  // Yes, there are such systems. This replacement relies on the fact that when
  // we call x_realpath we only care about symlinks.
  {
    int len = readlink(path, ret, maxlen - 1);
    if (len == -1) {
      free(ret);
      return NULL;
    }
    ret[len] = 0;
    p = ret;
  }
#endif
  if (p) {
    p = x_strdup(p);
    free(ret);
    return p;
  }
  free(ret);
  return NULL;
}

// A getcwd that will returns an allocated buffer.
char*
gnu_getcwd(void)
{
  unsigned size = 128;

  while (true) {
    char* buffer = (char*)x_malloc(size);
    if (getcwd(buffer, size) == buffer) {
      return buffer;
    }
    free(buffer);
    if (errno != ERANGE) {
      cc_log("getcwd error: %d (%s)", errno, strerror(errno));
      return NULL;
    }
    size *= 2;
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

#ifndef HAVE_STRTOK_R
// strtok_r replacement.
char*
strtok_r(char* str, const char* delim, char** saveptr)
{
  if (!str) {
    str = *saveptr;
  }
  int len = strlen(str);
  char* ret = strtok(str, delim);
  if (ret) {
    char* save = ret;
    while (*save++) {
      // Do nothing.
    }
    if ((len + 1) == (intptr_t)(save - str)) {
      save--;
    }
    *saveptr = save;
  }
  return ret;
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

// Return current user's home directory, or NULL if it can't be determined.
const char*
get_home_directory(void)
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
  return NULL;
}

// Get the current directory by reading $PWD. If $PWD isn't sane, gnu_getcwd()
// is used. Caller frees.
char*
get_cwd(void)
{
  char* cwd = gnu_getcwd();
  if (!cwd) {
    return NULL;
  }

  char* pwd = getenv("PWD");
  if (!pwd) {
    return cwd;
  }

  auto st_pwd = Stat::stat(pwd);
  auto st_cwd = Stat::stat(cwd);
  if (!st_pwd || !st_cwd) {
    return cwd;
  }
  if (st_pwd.device() == st_cwd.device() && st_pwd.inode() == st_cwd.inode()) {
    free(cwd);
    return x_strdup(pwd);
  } else {
    return cwd;
  }
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

// Compute the length of the longest directory path that is common to two
// paths. s1 is assumed to be the path to a directory.
size_t
common_dir_prefix_length(const char* s1, const char* s2)
{
  const char* p1 = s1;
  const char* p2 = s2;

  while (*p1 && *p2 && *p1 == *p2) {
    ++p1;
    ++p2;
  }
  while ((*p1 && *p1 != '/') || (*p2 && *p2 != '/')) {
    p1--;
    p2--;
  }
  if (!*p1 && !*p2 && p2 == s2 + 1) {
    // Special case for s1 and s2 both being "/".
    return 0;
  }
  return p1 - s1;
}

// Compute a relative path from from (an absolute path to a directory) to to (a
// path). Assumes that both from and to are well-formed and canonical. Caller
// frees.
char*
get_relative_path(const char* from, const char* to)
{
  size_t common_prefix_len;
  char* result;

  assert(from && is_absolute_path(from));
  assert(to);

  if (!*to || !is_absolute_path(to)) {
    return x_strdup(to);
  }

#ifdef _WIN32
  // Paths can be escaped by a slash for use with -isystem.
  if (from[0] == '/') {
    from++;
  }
  if (to[0] == '/') {
    to++;
  }
  // Both paths are absolute, drop the drive letters.
  assert(from[0] == to[0]); // Assume the same drive letter.
  from += 2;
  to += 2;
#endif

  result = x_strdup("");
  common_prefix_len = common_dir_prefix_length(from, to);
  if (common_prefix_len > 0 || !str_eq(from, "/")) {
    const char* p;
    for (p = from + common_prefix_len; *p; p++) {
      if (*p == '/') {
        reformat(&result, "../%s", result);
      }
    }
  }
  if (strlen(to) > common_prefix_len) {
    reformat(&result, "%s%s", result, to + common_prefix_len + 1);
  }
  for (int i = strlen(result) - 1; i >= 0 && result[i] == '/'; i--) {
    result[i] = '\0';
  }
  if (str_eq(result, "")) {
    free(result);
    result = x_strdup(".");
  }
  return result;
}

// Return whether path is absolute.
bool
is_absolute_path(const char* path)
{
#ifdef _WIN32
  return path[0] && path[1] == ':';
#else
  return path[0] == '/';
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
  utimes(path, NULL);
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
    LPVOID lp_msg_buf;
    DWORD dw = GetLastError();
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  dw,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&lp_msg_buf,
                  0,
                  NULL);

    LPVOID lp_display_buf = (LPVOID)LocalAlloc(
      LMEM_ZEROINIT,
      (lstrlen((LPCTSTR)lp_msg_buf) + lstrlen((LPCTSTR)__FILE__) + 40)
        * sizeof(TCHAR));
    _snprintf((LPTSTR)lp_display_buf,
              LocalSize(lp_display_buf) / sizeof(TCHAR),
              TEXT("%s failed with error %lu: %s"),
              __FILE__,
              dw,
              (const char*)lp_msg_buf);

    cc_log("can't rename file %s to %s OS returned error: %s",
           oldpath,
           newpath,
           (char*)lp_display_buf);

    LocalFree(lp_msg_buf);
    LocalFree(lp_display_buf);
    return -1;
  } else {
    return 0;
  }
#endif
}

// Remove path, NFS hazardous. Use only for temporary files that will not exist
// on other systems. That is, the path should include tmp_string().
int
tmp_unlink(const char* path)
{
  cc_log("Unlink %s", path);
  int rc = unlink(path);
  if (rc) {
    cc_log("Unlink failed: %s", strerror(errno));
  }
  return rc;
}

static int
do_x_unlink(const char* path, bool log_failure)
{
  int saved_errno = 0;

  // If path is on an NFS share, unlink isn't atomic, so we rename to a temp
  // file. We don't care if the temp file is trashed, so it's always safe to
  // unlink it first.
  char* tmp_name = format("%s.rm.%s", path, tmp_string());

  int result = 0;
  if (x_rename(path, tmp_name) == -1) {
    result = -1;
    saved_errno = errno;
    goto out;
  }
  if (unlink(tmp_name) == -1) {
    // If it was released in a race, that's OK.
    if (errno != ENOENT && errno != ESTALE) {
      result = -1;
      saved_errno = errno;
    }
  }

out:
  if (result == 0 || log_failure) {
    cc_log("Unlink %s via %s", path, tmp_name);
    if (result != 0 && log_failure) {
      cc_log("x_unlink failed: %s", strerror(saved_errno));
    }
  }
  free(tmp_name);
  errno = saved_errno;
  return result;
}

// Remove path, NFS safe, log both successes and failures.
int
x_unlink(const char* path)
{
  return do_x_unlink(path, true);
}

// Remove path, NFS safe, only log successes.
int
x_try_unlink(const char* path)
{
  return do_x_unlink(path, false);
}

#ifndef _WIN32
// Like readlink() but returns the string or NULL on failure. Caller frees.
char*
x_readlink(const char* path)
{
  long maxlen = path_max(path);
  char* buf = static_cast<char*>(x_malloc(maxlen));
  ssize_t len = readlink(path, buf, maxlen - 1);
  if (len == -1) {
    free(buf);
    return NULL;
  }
  buf[len] = 0;
  return buf;
}
#endif

// Reads the content of a file. Size hint 0 means no hint. Returns true on
// success, otherwise false.
bool
read_file(const char* path, size_t size_hint, char** data, size_t* size)
{
  if (size_hint == 0) {
    size_hint = Stat::stat(path, Stat::OnError::log).size();
  }
  size_hint = (size_hint < 1024) ? 1024 : size_hint;

  int fd = open(path, O_RDONLY | O_BINARY);
  if (fd == -1) {
    return false;
  }
  size_t allocated = size_hint;
  *data = static_cast<char*>(x_malloc(allocated));
  int ret;
  size_t pos = 0;
  while (true) {
    if (pos > allocated / 2) {
      allocated *= 2;
      *data = static_cast<char*>(x_realloc(*data, allocated));
    }
    ret = read(fd, *data + pos, allocated - pos);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
      break;
    }
    if (ret > 0) {
      pos += ret;
    }
  }
  close(fd);
  if (ret == -1) {
    cc_log("Failed reading %s", path);
    free(*data);
    *data = NULL;
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
    return NULL;
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
  *errmsg = NULL;

  char* result = x_strdup("");
  const char* p = str; // Interval start.
  const char* q = str; // Interval end.
  for (q = str; *q; ++q) {
    if (*q == '$') {
      reformat(&result, "%s%.*s", result, (int)(q - p), p);
      if (!expand_variable(&q, &result, errmsg)) {
        free(result);
        return NULL;
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
time_seconds(void)
{
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#else
  return (double)time(NULL);
#endif
}

std::string
from_cstr(const char* str)
{
  return str ? str : std::string{};
}
