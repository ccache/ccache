// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/filelock.hpp>

#include <fmt/core.h>
#include <fmt/format.h>

#include <filesystem>
#include <string_view>

// Log a raw message (plus a newline character).
#define LOG_RAW(message_)                                                      \
  do {                                                                         \
    if (util::logging::enabled()) {                                            \
      util::logging::log(std::string_view(message_));                          \
    }                                                                          \
  } while (false)

// Log a message (plus a newline character) described by a format string with at
// least one placeholder. `format` is checked at compile time.
#define LOG(format_, ...) LOG_RAW(fmt::format(FMT_STRING(format_), __VA_ARGS__))

// Log a message (plus a newline character) described by a format string with at
// least one placeholder without flushing and with a reused timestamp. `format`
// is checked at compile time.
#define BULK_LOG(logger_, format_, ...)                                        \
  logger_.log(fmt::format(FMT_STRING(format_), __VA_ARGS__))

namespace util::logging {

// Initialize global logging state. Must be called once before using the other
// logging functions.
void init(bool debug, const std::filesystem::path& log_file);

// Return whether logging is enabled to at least one destination.
bool enabled();

// Log `message` (plus a newline character).
void log(std::string_view message);

// Write the current log memory buffer to `path`.
void dump_log(const std::filesystem::path& path);

class BulkLogger
{
public:
  BulkLogger();

  // Log `message` (plus a newline character) with a reused timestamp.
  void log(std::string_view message);

private:
  util::FileLock m_file_lock;
};

} // namespace util::logging
