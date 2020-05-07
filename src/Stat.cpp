// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "Stat.hpp"

#include "logging.hpp"

#include "third_party/fmt/core.h"

Stat::Stat(StatFunction stat_function,
           const std::string& path,
           Stat::OnError on_error)
{
  int result = stat_function(path.c_str(), &m_stat);
  if (result == 0) {
    m_errno = 0;
  } else {
    m_errno = errno;
    if (on_error == OnError::throw_error) {
      throw Error(fmt::format("failed to stat {}: {}", path, strerror(errno)));
    }
    if (on_error == OnError::log) {
      cc_log("Failed to stat %s: %s", path.c_str(), strerror(errno));
    }

    // The file is missing, so just zero fill the stat structure. This will
    // make e.g. the is_*() methods return false and mtime() will be 0, etc.
    memset(&m_stat, '\0', sizeof(m_stat));
  }
}
