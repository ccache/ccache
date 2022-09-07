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

#include <Fd.hpp>
#include <Logging.hpp>
#include <Stat.hpp>
#include <fmtmacros.hpp>
#include <util/Bytes.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

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

#include <cerrno>
#include <codecvt>
#include <cstring>
#include <fstream>
#include <locale>
#include <type_traits>

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
  const auto result = util::write_file(path, cachedir_tag);
  if (!result) {
    LOG("Failed to create {}: {}", path, result.error());
  }
}

nonstd::expected<void, std::string>
read_fd(int fd, DataReceiver data_receiver)
{
  int64_t n;
  char buffer[CCACHE_READ_BUFFER_SIZE];
  while ((n = read(fd, buffer, sizeof(buffer))) != 0) {
    if (n == -1 && errno != EINTR) {
      break;
    }
    if (n > 0) {
      data_receiver(buffer, n);
    }
  }
  if (n == -1) {
    return nonstd::make_unexpected(strerror(errno));
  }
  return {};
}

#ifdef _WIN32
static bool
has_utf16_le_bom(std::string_view text)
{
  return text.size() > 1
         && ((static_cast<uint8_t>(text[0]) == 0xff
              && static_cast<uint8_t>(text[1]) == 0xfe));
}
#endif

template<typename T>
nonstd::expected<T, std::string>
read_file(const std::string& path, size_t size_hint)
{
  if (size_hint == 0) {
    const auto stat = Stat::stat(path);
    if (!stat) {
      LOG("Failed to stat {}: {}", path, strerror(errno));
      return nonstd::make_unexpected(strerror(errno));
    }
    size_hint = stat.size();
  }

  // +1 to be able to detect EOF in the first read call
  size_hint = (size_hint < 1024) ? 1024 : size_hint + 1;

  const int open_flags = [] {
    if constexpr (std::is_same<T, std::string>::value) {
      return O_RDONLY | O_TEXT;
    } else {
      return O_RDONLY | O_BINARY;
    }
  }();
  Fd fd(open(path.c_str(), open_flags));
  if (!fd) {
    LOG("Failed to open {}: {}", path, strerror(errno));
    return nonstd::make_unexpected(strerror(errno));
  }

  int64_t ret = 0;
  size_t pos = 0;
  T result;
  result.resize(size_hint);

  while (true) {
    if (pos == result.size()) {
      result.resize(2 * result.size());
    }
    const size_t max_read = result.size() - pos;
    ret = read(*fd, &result[pos], max_read);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
      break;
    }
    if (ret > 0) {
      pos += ret;
      if (static_cast<size_t>(ret) < max_read) {
        break;
      }
    }
  }

  if (ret == -1) {
    LOG("Failed to read {}: {}", path, strerror(errno));
    return nonstd::make_unexpected(strerror(errno));
  }

  result.resize(pos);

#ifdef _WIN32
  if constexpr (std::is_same<T, std::string>::value) {
    // Convert to UTF-8 if the content starts with a UTF-16 little-endian BOM.
    //
    // Note that this code assumes a little-endian machine, which is why it's
    // #ifdef-ed to only run on Windows (which is always little-endian) where
    // it's actually needed.
    if (has_utf16_le_bom(result)) {
      result.erase(0, 2); // Remove BOM.
      std::u16string result_as_u16((result.size() / 2) + 1, '\0');
      result_as_u16 = reinterpret_cast<const char16_t*>(result.c_str());
      std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>
        converter;
      result = converter.to_bytes(result_as_u16);
    }
  }
#endif

  return result;
}

template nonstd::expected<util::Bytes, std::string>
read_file(const std::string& path, size_t size_hint);

template nonstd::expected<std::string, std::string>
read_file(const std::string& path, size_t size_hint);

template nonstd::expected<std::vector<uint8_t>, std::string>
read_file(const std::string& path, size_t size_hint);

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

nonstd::expected<void, std::string>
write_fd(int fd, const void* data, size_t size)
{
  int64_t written = 0;
  do {
    const auto count =
      write(fd, static_cast<const uint8_t*>(data) + written, size - written);
    if (count == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        return nonstd::make_unexpected(strerror(errno));
      }
    } else {
      written += count;
    }
  } while (static_cast<size_t>(written) < size);
  return {};
}

nonstd::expected<void, std::string>
write_file(const std::string& path, std::string_view data, InPlace in_place)
{
  if (in_place == InPlace::no) {
    unlink(path.c_str());
  }
  Fd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_TEXT, 0666));
  if (!fd) {
    return nonstd::make_unexpected(strerror(errno));
  }
  return write_fd(*fd, data.data(), data.size());
}

nonstd::expected<void, std::string>
write_file(const std::string& path,
           nonstd::span<const uint8_t> data,
           InPlace in_place)
{
  if (in_place == InPlace::no) {
    unlink(path.c_str());
  }
  Fd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
  if (!fd) {
    return nonstd::make_unexpected(strerror(errno));
  }
  return write_fd(*fd, data.data(), data.size());
}

} // namespace util
