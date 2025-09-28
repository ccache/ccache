// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include "process.hpp"

#include <ccache/util/wincompat.hpp>

#include <cstring>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace {

// Process umask, read and written by get_umask and set_umask.
mode_t g_umask = [] {
  const mode_t mask = umask(0);
  umask(mask);
  return mask;
}();

} // namespace

namespace util {

const char*
get_hostname()
{
  static char hostname[260] = "";

  if (hostname[0]) {
    return hostname;
  }

  if (gethostname(hostname, sizeof(hostname)) != 0) {
    strcpy(hostname, "unknown");
  }
  hostname[sizeof(hostname) - 1] = 0;
  return hostname;
}

mode_t
get_umask()
{
  return g_umask;
}

mode_t
set_umask(mode_t mask)
{
  g_umask = mask;
  return umask(mask);
}

} // namespace util
