// Copyright (C) 2020-2021 Joel Rosdahl and other contributors
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

#include <core/exceptions.hpp>

#ifdef _WIN32
#  include "third_party/win32/mktemp.h"
#endif

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
#ifdef _WIN32
  // MSVC lacks mkstemp() and Mingw-w64's implementation[1] is problematic, as
  // it can reuse the names of recently-deleted files unless the caller
  // remembers to call srand().

  // [1]: <https://github.com/Alexpux/mingw-w64/blob/
  // d0d7f784833bbb0b2d279310ddc6afb52fe47a46/mingw-w64-crt/misc/mkstemp.c>
  fd = Fd(bsd_mkstemp(&path[0]));
#else
  fd = Fd(mkstemp(&path[0]));
#endif
  if (!fd) {
    throw core::Fatal(
      "Failed to create temporary file for {}: {}", path, strerror(errno));
  }

  Util::set_cloexec_flag(*fd);
#ifndef _WIN32
  fchmod(*fd, 0666 & ~get_umask());
#endif
}
