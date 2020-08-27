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

#include "Win32Util.hpp"

#include "Util.hpp"

#include <chrono>
#include <thread>

namespace Win32Util {

std::string
add_exe_suffix(const std::string& path)
{
  auto ext = Util::to_lowercase(Util::get_extension(path));
  if (ext == ".exe" || ext == ".bat" || ext == ".sh") {
    return path;
  } else {
    return path + ".exe";
  }
}

std::string
error_message(DWORD error_code)
{
  LPSTR buffer;
  size_t size =
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                     | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,
                   error_code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&buffer),
                   0,
                   nullptr);
  std::string message(buffer, size);
  LocalFree(buffer);
  return message;
}

std::string
argv_to_string(const char* const* argv, const std::string& prefix)
{
  std::string result;
  size_t i = 0;
  const char* arg = prefix.empty() ? argv[i++] : prefix.c_str();

  do {
    int bs = 0;
    result += '"';
    for (size_t j = 0; arg[j]; ++j) {
      switch (arg[j]) {
      case '\\':
        ++bs;
        break;
      // Fallthrough.
      case '"':
        bs = (bs << 1) + 1;
      // Fallthrough.
      default:
        while (bs > 0) {
          result += '\\';
          --bs;
        }
        result += arg[j];
      }
    }
    bs <<= 1;
    while (bs > 0) {
      result += '\\';
      --bs;
    }
    result += "\" ";
  } while ((arg = argv[i++]));

  result.resize(result.length() - 1);
  return result;
}

} // namespace Win32Util

// From: https://stackoverflow.com/a/58162122/262458
#ifdef _MSC_VER
int
gettimeofday(struct timeval* tp, struct timezone* /*tzp*/)
{
  namespace sc = std::chrono;
  sc::system_clock::duration d = sc::system_clock::now().time_since_epoch();
  sc::seconds s = sc::duration_cast<sc::seconds>(d);
  tp->tv_sec = static_cast<long>(s.count());
  tp->tv_usec =
    static_cast<long>(sc::duration_cast<sc::microseconds>(d - s).count());

  return 0;
}
#endif

void
usleep(int64_t usec)
{
  std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

struct tm*
localtime_r(time_t* _clock, struct tm* _result)
{
  struct tm* p = localtime(_clock);

  if (p)
    *(_result) = *p;

  return p;
}

// From: https://stackoverflow.com/a/40160038/262458
#ifdef _MSC_VER
int
vasprintf(char** strp, const char* fmt, va_list ap)
{
  // _vscprintf tells you how big the buffer needs to be
  int len = _vscprintf(fmt, ap);
  if (len == -1) {
    return -1;
  }
  size_t size = (size_t)len + 1;
  char* str = static_cast<char*>(malloc(size));
  if (!str) {
    return -1;
  }
  // vsprintf_s is the "secure" version of vsprintf
  int r = vsprintf_s(str, len + 1, fmt, ap);
  if (r == -1) {
    free(str);
    return -1;
  }
  *strp = str;
  return r;
}
#endif

// Also from: https://stackoverflow.com/a/40160038/262458
#ifdef _MSC_VER
int
asprintf(char** strp, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int r = vasprintf(strp, fmt, ap);
  va_end(ap);
  return r;
}
#endif
