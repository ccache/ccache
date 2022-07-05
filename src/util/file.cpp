// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#ifdef HAVE_UTIMENSAT
#  include <fcntl.h>
#  include <sys/stat.h>
#elif defined(HAVE_UTIMES)
#  include <sys/time.h>
#else
#  include <sys/types.h>
#  ifdef HAVE_UTIME_H
#    include <utime.h>
#  elif defined(HAVE_SYS_UTIME_H)
#    include <sys/utime.h>
#  endif
#endif

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

void
set_timestamps(const std::string& path,
               std::optional<timespec> mtime,
               std::optional<timespec> atime)
{
#ifdef HAVE_UTIMENSAT
  timespec atime_mtime[2];
  if (mtime) {
    atime_mtime[0] = atime ? *atime : *mtime;
    atime_mtime[1] = *mtime;
  }
  utimensat(AT_FDCWD, path.c_str(), mtime ? atime_mtime : nullptr, 0);
#elif defined(HAVE_UTIMES)
  timeval atime_mtime[2];
  if (mtime) {
    atime_mtime[0].tv_sec = atime ? atime->tv_sec : mtime->tv_sec;
    atime_mtime[0].tv_usec = (atime ? atime->tv_nsec : mtime->tv_nsec) / 1000;
    atime_mtime[1].tv_sec = mtime->tv_sec;
    atime_mtime[1].tv_usec = mtime->tv_nsec / 1000;
  }
  utimes(path.c_str(), mtime ? atime_mtime : nullptr);
#else
  utimbuf atime_mtime;
  if (mtime) {
    atime_mtime.actime = atime ? atime->tv_sec : mtime->tv_sec;
    atime_mtime.modtime = mtime->tv_sec;
    utime(path.c_str(), &atime_mtime);
  } else {
    utime(path.c_str(), nullptr);
  }
#endif
}

} // namespace util
