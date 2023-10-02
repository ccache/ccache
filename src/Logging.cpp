// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2009-2023 Joel Rosdahl and other contributors
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

#include "Logging.hpp"

#include "Config.hpp"
#include "Util.hpp"
#include "execute.hpp"

#include <util/FileStream.hpp>
#include <util/file.hpp>
#include <util/fmtmacros.hpp>
#include <util/time.hpp>
#include <util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
#endif

#ifdef __linux__
#  ifdef HAVE_SYS_IOCTL_H
#    include <sys/ioctl.h>
#  endif
#endif

namespace {

// Logfile path and file handle, read from Config::log_file().
std::string logfile_path;
util::FileStream logfile;

// Whether to use syslog() instead.
bool use_syslog = false;

// Buffer used for logs in debug mode.
std::string debug_log_buffer;

// Whether debug logging is enabled via configuration or environment variable.
bool debug_log_enabled = false;

// Print error message to stderr about failure writing to the log file and exit
// with failure.
[[noreturn]] void
print_fatal_error_and_exit()
{
  // Note: Can't throw Fatal since that would lead to recursion.
  try {
    PRINT(stderr,
          "ccache: error: Failed to write to {}: {}\n",
          logfile_path,
          strerror(errno));
  } catch (std::runtime_error&) {
    // Ignore since we can't do anything about it.
  }
  exit(EXIT_FAILURE);
}

void
do_log(std::string_view message, bool bulk)
{
  static char prefix[200];

  if (!bulk) {
    char timestamp[100];
    auto now = util::TimePoint::now();
    auto tm = util::localtime(now);
    if (tm) {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &*tm);
    } else {
      snprintf(timestamp,
               sizeof(timestamp),
               "%llu",
               static_cast<long long unsigned int>(now.sec()));
    }
    snprintf(prefix,
             sizeof(prefix),
             "[%s.%06d %-5d] ",
             timestamp,
             static_cast<unsigned int>(now.nsec_decimal_part() / 1000),
             static_cast<int>(getpid()));
  }

  if (logfile
      && (fputs(prefix, *logfile) == EOF
          || fwrite(message.data(), message.length(), 1, *logfile) != 1
          || fputc('\n', *logfile) == EOF
          || (!bulk && fflush(*logfile) == EOF))) {
    print_fatal_error_and_exit();
  }
#ifdef HAVE_SYSLOG
  if (use_syslog) {
    // Note: No log prefix since syslog will add a prefix of its own.
    syslog(
      LOG_DEBUG, "%.*s", static_cast<int>(message.length()), message.data());
    // Note: No trailing newline.
  }
#endif
  if (debug_log_enabled) {
    debug_log_buffer += prefix;
    debug_log_buffer.append(message.data(), message.length());
    debug_log_buffer += '\n';
  }
}

} // namespace

namespace Logging {

// Initialize logging. Call only once.
void
init(const Config& config)
{
  debug_log_enabled = config.debug();

#ifdef HAVE_SYSLOG
  if (config.log_file() == "syslog") {
    use_syslog = true;
    openlog("ccache", LOG_PID, LOG_USER);
    return; // Don't open logfile
  }
#endif

  if (!config.log_file().empty()) {
    logfile_path = config.log_file();
    logfile.open(logfile_path, "a");
    if (logfile) {
      util::set_cloexec_flag(fileno(*logfile));
    } else {
      print_fatal_error_and_exit();
    }
  }
}

bool
enabled()
{
  return debug_log_enabled || logfile || use_syslog;
}

void
log(std::string_view message)
{
  if (!enabled()) {
    return;
  }
  do_log(message, false);
}

void
bulk_log(std::string_view message)
{
  if (!enabled()) {
    return;
  }
  do_log(message, true);
}

void
dump_log(const std::string& path)
{
  if (!enabled()) {
    return;
  }
  util::FileStream file(path, "w");
  if (file) {
    (void)fwrite(debug_log_buffer.data(), debug_log_buffer.length(), 1, *file);
  } else {
    LOG("Failed to open {}: {}", path, strerror(errno));
  }
}

} // namespace Logging
