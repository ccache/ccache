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

#include "file.hpp"

#include <Logging.hpp>
#include <Util.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>

namespace util {

void
create_cachedir_tag(const std::string& dir)
{
  constexpr char cachedir_tag[] =
    "Signature: 8a477f597d28d172789f06886806bc55\n"
    "# This file is a cache directory tag created by ccache.\n"
    "# For information about cache directory tags, see:\n"
    "#\thttp://www.brynosaurus.com/cachedir/\n";

  const std::string path = FMT("{}/CACHEDIR.TAG", dir);
  const auto stat = Stat::stat(path);
  if (stat) {
    return;
  }
  try {
    Util::write_file(path, cachedir_tag);
  } catch (const core::Error& e) {
    LOG("Failed to create {}: {}", path, e.what());
  }
}

} // namespace util
