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

#pragma once

#include <ccache/core/statisticscounters.hpp>

#include <filesystem>
#include <functional>
#include <optional>

namespace storage::local {

class StatsFile
{
public:
  explicit StatsFile(const std::filesystem::path& path);

  // Read counters. No lock is acquired. If the file doesn't exist all returned
  // counters will be zero.
  core::StatisticsCounters read() const;

  enum class OnlyIfChanged { no, yes };

  // Acquire a lock, read counters, call `function` with the counters, write the
  // counters and release the lock. Returns the resulting counters or nullopt on
  // error (e.g. if the lock could not be acquired).
  std::optional<core::StatisticsCounters>
  update(std::function<void(core::StatisticsCounters& counters)>,
         OnlyIfChanged only_if_changed = OnlyIfChanged::no) const;

private:
  std::filesystem::path m_path;
};

} // namespace storage::local
