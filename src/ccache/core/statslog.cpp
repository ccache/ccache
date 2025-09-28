// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "statslog.hpp"

#include <ccache/core/statistics.hpp>
#include <ccache/util/filestream.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>

#include <cstring>
#include <fstream>

namespace fs = util::filesystem;

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
StatsLog::log_result(const fs::path& input_file,
                     const std::vector<std::string>& result_ids)
{
  util::FileStream file(m_path, "ab");
  if (!file) {
    LOG("Failed to open {}: {}", m_path, strerror(errno));
    return;
  }

  PRINT(*file, "# {}\n", input_file);
  for (const auto& id : result_ids) {
    PRINT(*file, "{}\n", id);
  }
}

} // namespace core
