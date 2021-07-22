// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include "StatsLog.hpp"

#include <File.hpp>
#include <Logging.hpp>
#include <core/Statistics.hpp>
#include <fmtmacros.hpp>

#include <fstream>

namespace core {

StatisticsCounters
StatsLog::read() const
{
  core::StatisticsCounters counters;

  const auto id_map = Statistics::get_id_map();

  std::ifstream in(m_path);
  std::string line;
  while (std::getline(in, line, '\n')) {
    if (line[0] == '#') {
      continue;
    }
    const auto entry = id_map.find(line);
    if (entry != id_map.end()) {
      Statistic statistic = entry->second;
      counters.increment(statistic, 1);
    } else {
      LOG("Unknown statistic: {}", line);
    }
  }

  return counters;
}

void
StatsLog::log_result(const std::string& input_file,
                     const std::string& result_id)
{
  File file(m_path, "ab");
  if (file) {
    PRINT(*file, "# {}\n", input_file);
    PRINT(*file, "{}\n", result_id);
  } else {
    LOG("Failed to open {}: {}", m_path, strerror(errno));
  }
}

} // namespace core
