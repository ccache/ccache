// Copyright (C) 2023-2025 Joel Rosdahl and other contributors
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

#include "time.hpp"

namespace util {

std::optional<tm>
gmtime(std::optional<TimePoint> time)
{
  time_t timestamp = time ? sec(*time) : sec(now());
#ifdef HAVE_GMTIME_R
  struct tm result;
  if (gmtime_r(&timestamp, &result)) {
    return result;
  }
#else
  struct tm* result = ::gmtime(&timestamp);
  if (result) {
    return *result;
  }
#endif
  return std::nullopt;
}

std::optional<tm>
localtime(std::optional<TimePoint> time)
{
  time_t timestamp = time ? sec(*time) : sec(now());
#ifdef HAVE_LOCALTIME_R
  struct tm result;
  if (localtime_r(&timestamp, &result)) {
    return result;
  }
#else
  struct tm* result = ::localtime(&timestamp);
  if (result) {
    return *result;
  }
#endif
  return std::nullopt;
}

} // namespace util
