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

#include "statsfile.hpp"

#include <ccache/core/atomicfile.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/lockfile.hpp>
#include <ccache/util/logging.hpp>

namespace fs = util::filesystem;

namespace storage::local {

StatsFile::StatsFile(const fs::path& path)
  : m_path(path)
{
}

core::StatisticsCounters
StatsFile::read() const
{
  core::StatisticsCounters counters;

  const auto data = util::read_file<std::string>(m_path);
  if (!data) {
    // A nonexistent stats file is OK.
    return counters;
  }

  size_t i = 0;
  const char* str = data->c_str();
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
  std::function<void(core::StatisticsCounters& counters)> function,
  OnlyIfChanged only_if_changed) const
{
  util::LockFile lock(m_path);
  if (!lock.acquire()) {
    LOG("Failed to acquire lock for {}", m_path);
    return std::nullopt;
  }

  auto counters = read();
  const auto orig_counters = counters;
  function(counters);
  if (only_if_changed == OnlyIfChanged::no || counters != orig_counters) {
    core::AtomicFile file(m_path, core::AtomicFile::Mode::text);
    for (size_t i = 0; i < counters.size(); ++i) {
      file.write(FMT("{}\n", counters.get_raw(i)));
    }
    try {
      file.commit();
    } catch (const core::Error& e) {
      // Make failure to write a stats file a soft error since it's not
      // important enough to fail whole the process and also because it is
      // called in the Context destructor.
      LOG("Error: {}", e.what());
    }
  }

  return counters;
}

} // namespace storage::local
