// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include "StatisticsCounters.hpp"

#include <string>
#include <vector>

namespace core {

class StatsLog
{
public:
  explicit StatsLog(const std::string& path);

  StatisticsCounters read() const;
  void log_result(const std::string& input_file,
                  const std::vector<std::string>& result_ids);

private:
  const std::string m_path;
};

// --- Inline implementations ---

inline StatsLog::StatsLog(const std::string& path) : m_path(path)
{
}

} // namespace core
