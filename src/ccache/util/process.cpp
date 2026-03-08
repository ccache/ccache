// Copyright (C) 2023-2026 Joel Rosdahl and other contributors
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

#include <ccache/util/filesystem.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

#include <climits>
#include <cstring>
#include <vector>

namespace fs = util::filesystem;

namespace {

// Process umask, read and written by get_umask and set_umask.
mode_t g_umask = [] {
  const mode_t mask = umask(0);
  umask(mask);
  return mask;
}();

} // namespace

namespace util {

fs::path
get_executable_path()
{
#ifdef _WIN32
  std::vector<wchar_t> buf(MAX_PATH);
  while (true) {
    DWORD len =
      GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) {
      return {};
    }
    if (len < static_cast<DWORD>(buf.size())) {
      return fs::path(std::wstring(buf.data(), len));
    }
    buf.resize(buf.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = PATH_MAX;
  std::vector<char> buf(size);
  if (_NSGetExecutablePath(buf.data(), &size) != 0) {
    buf.resize(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
      return {};
    }
  }
  return fs::path(buf.data());
#else
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, PATH_MAX - 1);
  if (len < 0) {
    return {};
  }
  buf[len] = '\0';
  return fs::path(buf);
#endif
}

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
