// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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
#include <fmtmacros.hpp>

#include <cstdlib>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef _WIN32
#  include "third_party/win32/mktemp.h"
#endif

TemporaryFile::TemporaryFile(std::string_view path_prefix,
                             std::string_view suffix)
  : path(FMT("{}.XXXXXX{}", path_prefix, suffix))
{
  Util::ensure_dir_exists(Util::dir_name(path));
#ifdef _WIN32
  // MSVC lacks mkstemps() and Mingw-w64's implementation[1] is problematic, as
  // it can reuse the names of recently-deleted files unless the caller
  // remembers to call srand().

  // [1]: <https://github.com/Alexpux/mingw-w64/blob/
  // d0d7f784833bbb0b2d279310ddc6afb52fe47a46/mingw-w64-crt/misc/mkstemp.c>
  fd = Fd(bsd_mkstemps(&path[0], suffix.length()));
#else
  fd = Fd(mkstemps(&path[0], suffix.length()));
#endif
  if (!fd) {
    throw core::Fatal(
      "Failed to create temporary file for {}: {}", path, strerror(errno));
  }

  Util::set_cloexec_flag(*fd);
#ifndef _WIN32
  fchmod(*fd, 0666 & ~Util::get_umask());
#endif
}
