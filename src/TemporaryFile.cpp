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

#include "TemporaryFile.hpp"

#include "Util.hpp"

using nonstd::string_view;

namespace {

#ifndef _WIN32
mode_t
get_umask()
{
  static bool mask_retrieved = false;
  static mode_t mask;
  if (!mask_retrieved) {
    mask = umask(0);
    umask(mask);
    mask_retrieved = true;
  }
  return mask;
}
#endif

} // namespace

TemporaryFile::TemporaryFile(string_view path_prefix)
  : path(std::string(path_prefix) + ".XXXXXX")
{
  Util::ensure_dir_exists(Util::dir_name(path));
  fd = Fd(mkstemp(&path[0]));
  if (!fd) {
    throw Fatal(
      "Failed to create temporary file for {}: {}", path, strerror(errno));
  }

  Util::set_cloexec_flag(*fd);
#ifndef _WIN32
  fchmod(*fd, 0666 & ~get_umask());
#endif
}
