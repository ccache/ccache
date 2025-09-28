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

#include "umaskscope.hpp"

#include <ccache/util/process.hpp>

#include <sys/stat.h>

namespace util {

UmaskScope::UmaskScope(std::optional<mode_t> new_umask)
{
#ifndef _WIN32
  if (new_umask) {
    m_saved_umask = set_umask(*new_umask);
  }
#else
  (void)new_umask;
#endif
}

void
UmaskScope::release()
{
#ifndef _WIN32
  if (m_saved_umask) {
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
#  if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#  endif
    set_umask(*m_saved_umask);
#  if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#  endif
    m_saved_umask = std::nullopt;
  }
#endif
}

} // namespace util
