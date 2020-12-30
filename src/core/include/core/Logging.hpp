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

#pragma once

#include "core/system.hpp"

#include "core/FormatNonstdStringView.hpp"

#include "third_party/fmt/core.h"
#include "third_party/fmt/format.h"
#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#include <string>
#include <utility>

// Log a raw message (plus a newline character).
#define LOG_RAW(message_)                                                      \
  do {                                                                         \
    if (Logging::enabled()) {                                                  \
      Logging::log(nonstd::string_view(message_));                             \
    }                                                                          \
  } while (false)

// Log a message (plus a newline character) described by a format string with at
// least one placeholder. `format` is compile-time checked if CMAKE_CXX_STANDARD
// >= 14.
#define LOG(format_, ...) LOG_RAW(fmt::format(FMT_STRING(format_), __VA_ARGS__))

// Log a message (plus a newline character) described by a format string with at
// least one placeholder without flushing and with a reused timestamp. `format`
// is compile-time checked if CMAKE_CXX_STANDARD >= 14.
#define BULK_LOG(format_, ...)                                                 \
  do {                                                                         \
    if (Logging::enabled()) {                                                  \
      Logging::bulk_log(fmt::format(FMT_STRING(format_), __VA_ARGS__));        \
    }                                                                          \
  } while (false)

class Config;

namespace Logging {

// Initialize global logging state. Must be called once before using the other
// logging functions.
void init(const Config& config);

// Return whether logging is enabled to at least one destination.
bool enabled();

// Log `message` (plus a newline character).
void log(nonstd::string_view message);

// Log `message` (plus a newline character) without flushing and with a reused
// timestamp.
void bulk_log(nonstd::string_view message);

// Write the current log memory buffer `path`.
void dump_log(const std::string& path);

} // namespace Logging
