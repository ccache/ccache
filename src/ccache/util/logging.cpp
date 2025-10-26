// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2009-2025 Joel Rosdahl and other contributors
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

#include <ccache/config.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filestream.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/time.hpp>

#include <mutex>
#include <string>

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

namespace fs = util::filesystem;

namespace {

// Logfile path and file handle, read from Config::log_file().
fs::path logfile_path;
util::FileStream logfile;

// Whether to use syslog() instead.
bool use_syslog = false;

// Buffer used for logs in debug mode.
std::string debug_log_buffer;

// Whether debug logging is enabled via configuration or environment variable.
bool debug_log_enabled = false;

// Mutex that serializes writes to logfile and protects debug_log_buffer.
std::mutex log_mutex;

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
  } catch (std::runtime_error&) { // NOLINT: this is deliberate
    // Ignore since we can't do anything about it.
  }
  exit(EXIT_FAILURE);
}

void
format_prefix(char* buffer, size_t size)
{
  const auto now = util::now();
  (void)snprintf(buffer,
                 size,
                 "[%s.%06u %-5d] ",
                 util::format_iso8601_timestamp(now).c_str(),
                 static_cast<unsigned int>(util::nsec_part(now) / 1000),
                 static_cast<int>(getpid()));
}

// Write log entry to all enabled destinations.
//
// Assumes log_mutex is already held and file_lock is acquired if needed.
void
write_log_entry(const char* prefix, std::string_view message)
{
  if (logfile) {
    if (fputs(prefix, *logfile) == EOF
        || fwrite(message.data(), message.length(), 1, *logfile) != 1
        || fputc('\n', *logfile) == EOF || fflush(*logfile) == EOF) {
      print_fatal_error_and_exit();
    }
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

namespace util::logging {

// Initialize logging. Call only once.
void
init(bool debug, const fs::path& log_file)
{
  debug_log_enabled = debug;

#ifdef HAVE_SYSLOG
  if (log_file == "syslog") {
    use_syslog = true;
    openlog("ccache", LOG_PID, LOG_USER);
    return; // Don't open logfile
  }
#endif

  if (!log_file.empty()) {
    logfile_path = log_file;
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

  char prefix[200];
  format_prefix(prefix, sizeof(prefix));

  std::unique_lock<std::mutex> lock(log_mutex);

  std::optional<util::FileLock> file_lock;
  if (logfile) {
    file_lock.emplace(fileno(*logfile));
    std::ignore = file_lock->acquire(); // Continue anyway on failure
  }

  write_log_entry(prefix, message);
}

void
dump_log(const fs::path& path)
{
  if (!enabled()) {
    return;
  }

  std::unique_lock<std::mutex> lock(log_mutex);
  util::FileStream file(path, "w");
  if (file) {
    (void)fwrite(debug_log_buffer.data(), debug_log_buffer.length(), 1, *file);
  }
}

BulkLogger::BulkLogger()
{
  if (enabled()) {
    // Compute and cache prefix once for all bulk logs.
    format_prefix(m_prefix, sizeof(m_prefix));

    // Acquire both locks: mutex for in-process sync, file lock for
    // inter-process.
    m_mutex_lock = std::unique_lock<std::mutex>(log_mutex);
    if (logfile) {
      m_file_lock.emplace(fileno(*logfile));
      std::ignore = m_file_lock->acquire(); // Continue anyway on failure
    }
  }
}

void
BulkLogger::log(std::string_view message)
{
  if (!enabled()) {
    return;
  }

  write_log_entry(m_prefix, message);
}

} // namespace util::logging
