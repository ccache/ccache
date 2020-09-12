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

#include "Statistics.hpp"

#include "AtomicFile.hpp"
#include "Lockfile.hpp"
#include "Logging.hpp"
#include "Util.hpp"
#include "exceptions.hpp"

using Logging::log;
using nonstd::nullopt;
using nonstd::optional;

namespace Statistics {

Counters
read(const std::string& path)
{
  Counters counters;

  std::string data;
  try {
    data = Util::read_file(path);
  } catch (const Error&) {
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

optional<Counters>
update(const std::string& path,
       std::function<void(Counters& counters)> function)
{
  Lockfile lock(path);
  if (!lock.acquired()) {
    log("failed to acquire lock for {}", path);
    return nullopt;
  }

  auto counters = Statistics::read(path);
  function(counters);

  AtomicFile file(path, AtomicFile::Mode::text);
  for (size_t i = 0; i < counters.size(); ++i) {
    file.write(fmt::format("{}\n", counters.get_raw(i)));
  }
  try {
    file.commit();
  } catch (const Error& e) {
    // Make failure to write a stats file a soft error since it's not
    // important enough to fail whole the process and also because it is
    // called in the Context destructor.
    log("Error: {}", e.what());
  }

  return counters;
}

} // namespace Statistics
