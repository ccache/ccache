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
#include "legacy_util.hpp"

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

#ifndef HAVE_MKSTEMP
// Cheap and nasty mkstemp replacement.
int
mkstemp(char* name_template)
{
#  ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  endif
  mktemp(name_template);
#  ifdef __GNUC__
#    pragma GCC diagnostic pop
#  endif
  return open(name_template, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
}
#endif

} // namespace

TemporaryFile::TemporaryFile(string_view path_prefix)
{
  if (!initialize(path_prefix) && errno == ENOENT) {
    auto dir = Util::dir_name(path);
    if (!Util::create_dir(dir)) {
      FATAL("Failed to create directory {}: {}", dir, strerror(errno));
    }
    initialize(path_prefix);
  }
  if (!fd) {
    FATAL("Failed to create temporary file for {}: {}", path, strerror(errno));
  }

  set_cloexec_flag(*fd);
#ifndef _WIN32
  fchmod(*fd, 0666 & ~get_umask());
#endif
}

bool
TemporaryFile::initialize(string_view path_prefix)
{
  path = std::string(path_prefix);
  path += ".XXXXXX";
  fd = Fd(mkstemp(const_cast<char*>(path.data()))); // cast needed before C++17
  return bool(fd);
}
