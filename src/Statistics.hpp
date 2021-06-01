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

#include "system.hpp"

#include "Counters.hpp"
#include "Statistic.hpp" // Any reasonable use of Statistics requires the Statistic enum.

#include "third_party/nonstd/optional.hpp"

#include <functional>
#include <string>

class Config;

namespace Statistics {

// Read counters from `path`. No lock is acquired.
Counters read(const std::string& path);

// Acquire a lock, read counters from `path`, call `function` with the counters,
// write the counters to `path` and release the lock. Returns the resulting
// counters or nullopt on error (e.g. if the lock could not be acquired).
nonstd::optional<Counters> update(const std::string& path,
                                  std::function<void(Counters& counters)>);

// Write input and result to the file in `path`.
void log_result(const std::string& path,
                const std::string& input,
                const std::string& result);

// Return a human-readable string representing the final ccache result, or
// nullopt if there was no result.
nonstd::optional<std::string> get_result_message(const Counters& counters);

// Return a machine-readable string representing the final ccache result, or
// nullopt if there was no result.
nonstd::optional<std::string> get_result_id(const Counters& counters);

// Zero all statistics counters except those tracking cache size and number of
// files in the cache.
void zero_all_counters(const Config& config);

// Collect cache statistics from all statistics counters.
std::pair<Counters, time_t> collect_counters(const Config& config);

// Format config header in human-readable format.
std::string format_config_header(const Config& config);

// Format cache statistics in human-readable format.
std::string format_human_readable(const Counters& counters,
                                  time_t last_updated);

// Format config footer in human-readable format.
std::string format_config_footer(const Config& config);

// Format cache statistics in machine-readable format.
std::string format_machine_readable(const Counters& counters,
                                    time_t last_updated);

} // namespace Statistics
