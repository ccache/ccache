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

#include "logging.hpp"

#include "Config.hpp"
#include "exceptions.hpp"
#include "execute.hpp"

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
#endif

#ifdef _WIN32
#  include <psapi.h>
#  include <sys/locking.h>
#  include <tchar.h>
#endif

// Destination for g_config.log_file.
static FILE* logfile;

// Path to the logfile.
static std::string logfile_path;

// Whether to use syslog() instead.
static bool use_syslog;

// Buffer used for logs in debug mode.
static char* debug_log_buffer;

// Allocated debug_log_buffer size.
static size_t debug_log_buffer_capacity;

// The amount of log data stored in debug_log_buffer.
static size_t debug_log_size;

#define DEBUG_LOG_BUFFER_MARGIN 1024

// Initialize logging. Call only once.
void
init_log(const Config& config)
{
  if (config.debug()) {
    debug_log_buffer_capacity = DEBUG_LOG_BUFFER_MARGIN;
    debug_log_buffer = static_cast<char*>(x_malloc(debug_log_buffer_capacity));
    debug_log_size = 0;
  }

#ifdef HAVE_SYSLOG
  if (config.log_file() == "syslog") {
    use_syslog = true;
    openlog("ccache", LOG_PID, LOG_USER);
    return; // Don't open logfile
  }
#endif

  logfile_path = config.log_file();
  logfile = fopen(logfile_path.c_str(), "a");
#ifndef _WIN32
  if (logfile) {
    set_cloexec_flag(fileno(logfile));
  }
#endif
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
    gettimeofday(&tv, nullptr);
    if (localtime_r((time_t*)&tv.tv_sec, &tm) != nullptr) {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
    } else {
      snprintf(timestamp, sizeof(timestamp), "%lu", tv.tv_sec);
    }
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

static void warn_log_fail() ATTR_NORETURN;

// Warn about failure writing to the log file and then exit.
static void
warn_log_fail()
{
  // Note: Can't call fatal() since that would lead to recursion.
  fprintf(stderr,
          "ccache: error: Failed to write to %s: %s\n",
          logfile_path.c_str(),
          strerror(errno));
  x_exit(EXIT_FAILURE);
}

static void
vlog(const char* format, va_list ap, bool log_updated_time)
{
  if (!(debug_log_buffer || logfile || use_syslog)) {
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
cc_log_argv(const char* prefix, const char* const* argv)
{
  if (!(debug_log_buffer || logfile || use_syslog)) {
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
