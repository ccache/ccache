// Copyright (C) 2020-2021 Joel Rosdahl and other contributors
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

#include <core/StatisticsCounters.hpp>

#include <third_party/nonstd/optional.hpp>

#include <string>
#include <unordered_map>

class Config;

namespace core {

class Statistics
{
public:
  Statistics(const StatisticsCounters& counters);

  // Return a machine-readable string representing the final ccache result, or
  // nullopt if there was no result.
  nonstd::optional<std::string> get_result_id() const;

  // Return a human-readable string representing the final ccache result, or
  // nullopt if there was no result.
  nonstd::optional<std::string> get_result_message() const;

  // Format config header in human-readable format.
  static std::string format_config_header(const Config& config);

  // Format cache statistics in human-readable format.
  std::string format_human_readable(time_t last_updated, bool from_log) const;

  // Format config footer in human-readable format.
  static std::string format_config_footer(const Config& config);

  // Format cache statistics in machine-readable format.
  std::string format_machine_readable(time_t last_updated) const;

  const StatisticsCounters& counters() const;

  static std::unordered_map<std::string, Statistic> get_id_map();

  static std::vector<Statistic> get_zeroable_fields();

private:
  const StatisticsCounters m_counters;
};

// --- Inline implementations ---

inline const StatisticsCounters&
Statistics::counters() const
{
  return m_counters;
}

} // namespace core
