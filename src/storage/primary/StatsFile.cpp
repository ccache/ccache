// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include "StatsFile.hpp"

#include <AtomicFile.hpp>
#include <Logging.hpp>
#include <Util.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/LockFile.hpp>

namespace storage::primary {

StatsFile::StatsFile(const std::string& path) : m_path(path)
{
}

core::StatisticsCounters
StatsFile::read() const
{
  core::StatisticsCounters counters;

  std::string data;
  try {
    data = Util::read_file(m_path);
  } catch (const core::Error&) {
    // Ignore.
    return counters;
  }

  size_t i = 0;
  const char* str = data.c_str();
  while (true) {
    char* end;
    const uint64_t value = std::strtoull(str, &end, 10);
    if (end == str) {
      break;
    }
    counters.set_raw(i, value);
    ++i;
    str = end;
  }

  return counters;
}

std::optional<core::StatisticsCounters>
StatsFile::update(
  std::function<void(core::StatisticsCounters& counters)> function) const
{
  util::ShortLivedLockFile lock_file(m_path);
  util::LockFileGuard lock(lock_file);
  if (!lock.acquired()) {
    LOG("Failed to acquire lock for {}", m_path);
    return std::nullopt;
  }

  auto counters = read();
  function(counters);

  AtomicFile file(m_path, AtomicFile::Mode::text);
  for (size_t i = 0; i < counters.size(); ++i) {
    file.write(FMT("{}\n", counters.get_raw(i)));
  }
  try {
    file.commit();
  } catch (const core::Error& e) {
    // Make failure to write a stats file a soft error since it's not important
    // enough to fail whole the process and also because it is called in the
    // Context destructor.
    LOG("Error: {}", e.what());
  }

  return counters;
}

} // namespace storage::primary
