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

#pragma once

#include <core/StatisticsCounters.hpp>

#include <third_party/nonstd/optional.hpp>

#include <functional>
#include <string>

namespace storage {
namespace primary {

class StatsFile
{
public:
  StatsFile(const std::string& path);

  // Read counters. No lock is acquired. If the file doesn't exist all returned
  // counters will be zero.
  core::StatisticsCounters read() const;

  // Acquire a lock, read counters, call `function` with the counters, write the
  // counters and release the lock. Returns the resulting counters or nullopt on
  // error (e.g. if the lock could not be acquired).
  nonstd::optional<core::StatisticsCounters>
    update(std::function<void(core::StatisticsCounters& counters)>) const;

private:
  const std::string m_path;
};

} // namespace primary
} // namespace storage
