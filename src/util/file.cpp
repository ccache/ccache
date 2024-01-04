// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include <util/Bytes.hpp>
#include <util/DirEntry.hpp>
#include <util/Fd.hpp>
#include <util/Finalizer.hpp>
#include <util/TemporaryFile.hpp>
#include <util/error.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/logging.hpp>

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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#endif

#include <cerrno>
#include <cstring>
#include <fstream>
#include <locale>
#include <type_traits>
#include <vector>

namespace fs = util::filesystem;

namespace util {

tl::expected<void, std::string>
copy_file(const fs::path& src, const fs::path& dest, ViaTmpFile via_tmp_file)
{
  Fd src_fd(open(src.string().c_str(), O_RDONLY | O_BINARY));
  if (!src_fd) {
    return tl::unexpected(
      FMT("Failed to open {} for reading: {}", src, strerror(errno)));
  }

  unlink(dest.string().c_str());

  Fd dest_fd;
  fs::path tmp_file;
  if (via_tmp_file == ViaTmpFile::yes) {
    auto temp_file = TemporaryFile::create(dest);
    if (!temp_file) {
      return tl::unexpected(temp_file.error());
    }
    dest_fd = std::move(temp_file->fd);
    tmp_file = std::move(temp_file->path);
  } else {
    dest_fd = Fd(open(
      dest.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      return tl::unexpected(
        FMT("Failed to open {} for writing: {}", dest, strerror(errno)));
    }
  }
  TRY(read_fd(*src_fd, [&](nonstd::span<const uint8_t> data) {
    write_fd(*dest_fd, data.data(), data.size());
  }));

  dest_fd.close();
  src_fd.close();

  if (via_tmp_file == ViaTmpFile::yes) {
    const auto result = fs::rename(tmp_file, dest);
    if (!result) {
      return tl::unexpected(FMT("Failed to rename {} to {}: {}",
                                tmp_file,
                                dest,
                                result.error().message()));
    }
  }

  return {};
}

void
create_cachedir_tag(const fs::path& dir)
{
  constexpr char cachedir_tag[] =
    "Signature: 8a477f597d28d172789f06886806bc55\n"
    "# This file is a cache directory tag created by ccache.\n"
    "# For information about cache directory tags, see:\n"
    "#\thttp://www.brynosaurus.com/cachedir/\n";

  auto path = dir / "CACHEDIR.TAG";
  if (fs::exists(path)) {
    return;
  }
  const auto result = write_file(path, cachedir_tag);
  if (!result) {
    LOG("Failed to create {}: {}", path, result.error());
  }
}

tl::expected<void, std::string>
fallocate(int fd, size_t new_size)
{
#ifdef HAVE_POSIX_FALLOCATE
  const int posix_fallocate_err = posix_fallocate(fd, 0, new_size);
  if (posix_fallocate_err == 0) {
    return {};
  }
  if (posix_fallocate_err != EINVAL) {
    return tl::unexpected(strerror(posix_fallocate_err));
  }
  // The underlying filesystem does not support the operation so fall back to
  // lseek.
#endif
  off_t saved_pos = lseek(fd, 0, SEEK_END);
  off_t old_size = lseek(fd, 0, SEEK_END);
  if (old_size == -1) {
    int err = errno;
    lseek(fd, saved_pos, SEEK_SET);
    return tl::unexpected(strerror(err));
  }
  if (static_cast<size_t>(old_size) >= new_size) {
    lseek(fd, saved_pos, SEEK_SET);
    return {};
  }
  long bytes_to_write = new_size - old_size;

  void* buf = calloc(bytes_to_write, 1);
  if (!buf) {
    lseek(fd, saved_pos, SEEK_SET);
    return tl::unexpected(strerror(ENOMEM));
  }
  Finalizer buf_freer([&] { free(buf); });

  return write_fd(fd, buf, bytes_to_write)
    .and_then([&]() -> tl::expected<void, std::string> {
      lseek(fd, saved_pos, SEEK_SET);
      return {};
    });
}

void
set_cloexec_flag(int fd)
{
#ifndef _WIN32
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
#else
  (void)fd;
#endif
}

tl::expected<void, std::string>
read_fd(int fd, DataReceiver data_receiver)
{
  int64_t n;
  uint8_t buffer[CCACHE_READ_BUFFER_SIZE];
  while ((n = read(fd, buffer, sizeof(buffer))) != 0) {
    if (n == -1 && errno != EINTR) {
      break;
    }
    if (n > 0) {
      data_receiver({buffer, static_cast<size_t>(n)});
    }
  }
  if (n == -1) {
    return tl::unexpected(strerror(errno));
  }
  return {};
}

tl::expected<Bytes, std::string>
read_fd(int fd)
{
  Bytes output;
  return read_fd(fd,
                 [&](auto data) {
                   output.insert(output.end(), data.begin(), data.end());
                 })
    .transform([&] { return output; });
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
tl::expected<T, std::string>
read_file(const fs::path& path, size_t size_hint)
{
  if (size_hint == 0) {
    DirEntry de(path);
    if (!de) {
      return tl::unexpected(strerror(errno));
    }
    size_hint = de.size();
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
  Fd fd(open(path.string().c_str(), open_flags));
  if (!fd) {
    return tl::unexpected(strerror(errno));
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
    return tl::unexpected(strerror(errno));
  }

  result.resize(pos);

#ifdef _WIN32
  if constexpr (std::is_same<T, std::string>::value) {
    // Convert to UTF-8 if the content starts with a UTF-16 little-endian BOM.
    if (has_utf16_le_bom(result)) {
      result.erase(0, 2); // Remove BOM.
      if (result.empty()) {
        return result;
      }

      std::wstring result_as_u16((result.size() / 2) + 1, '\0');
      result_as_u16 = reinterpret_cast<const wchar_t*>(result.c_str());
      const int size = WideCharToMultiByte(CP_UTF8,
                                           WC_ERR_INVALID_CHARS,
                                           result_as_u16.c_str(),
                                           int(result_as_u16.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr);
      if (size <= 0) {
        return tl::unexpected(
          FMT("Failed to convert {} from UTF-16LE to UTF-8: {}",
              path,
              util::win32_error_message(GetLastError())));
      }

      result = std::string(size, '\0');
      WideCharToMultiByte(CP_UTF8,
                          0,
                          result_as_u16.c_str(),
                          int(result_as_u16.size()),
                          &result.at(0),
                          size,
                          nullptr,
                          nullptr);
    }
  }
#endif

  return result;
}

template tl::expected<Bytes, std::string> read_file(const fs::path& path,
                                                    size_t size_hint);

template tl::expected<std::string, std::string> read_file(const fs::path& path,
                                                          size_t size_hint);

template tl::expected<std::vector<uint8_t>, std::string>
read_file(const fs::path& path, size_t size_hint);

template<typename T>
tl::expected<T, std::string>
read_file_part(const fs::path& path, size_t pos, size_t count)
{
  T result;
  if (count == 0) {
    return result;
  }

  Fd fd(open(path.string().c_str(), O_RDONLY | O_BINARY));
  if (!fd) {
    LOG("Failed to open {}: {}", path, strerror(errno));
    return tl::unexpected(strerror(errno));
  }

  if (pos != 0 && lseek(*fd, pos, SEEK_SET) != static_cast<off_t>(pos)) {
    return tl::unexpected(strerror(errno));
  }

  int64_t ret = 0;
  size_t bytes_read = 0;
  result.resize(count);

  while (true) {
    const size_t max_read = count - bytes_read;
    ret = read(*fd, &result[bytes_read], max_read);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
      break;
    }
    if (ret > 0) {
      bytes_read += ret;
      if (bytes_read == count) {
        break;
      }
    }
  }

  if (ret == -1) {
    LOG("Failed to read {}: {}", path, strerror(errno));
    return tl::unexpected(strerror(errno));
  }

  result.resize(bytes_read);
  return result;
}

template tl::expected<Bytes, std::string>
read_file_part(const fs::path& path, size_t pos, size_t count);

template tl::expected<std::string, std::string>
read_file_part(const fs::path& path, size_t pos, size_t count);

template tl::expected<std::vector<uint8_t>, std::string>
read_file_part(const fs::path& path, size_t pos, size_t count);

tl::expected<bool, std::error_code>
remove(const fs::path& path, LogFailure log_failure)
{
  auto result = fs::remove(path);
  if (result || log_failure == LogFailure::yes) {
    LOG("Removing {}", path);
    if (!result) {
      LOG("Removal failed: {}", result.error().message());
    }
  }
  return result;
}

tl::expected<bool, std::error_code>
remove_nfs_safe(const fs::path& path, LogFailure log_failure)
{
  // fs::remove isn't atomic if path is on an NFS share, so we rename to a
  // temporary file. We don't care if the temporary file is trashed, so it's
  // always safe to remove it first.
  auto tmp_path =
    path.parent_path()
    / FMT("{}.ccache{}remove", path.filename(), TemporaryFile::tmp_file_infix);

  auto rename_result = fs::rename(path, tmp_path);
  if (!rename_result) {
    // It's OK if it was removed in a race.
    if (rename_result.error().value() != ENOENT
        && rename_result.error().value() != ESTALE
        && log_failure == LogFailure::yes) {
      LOG("Removing {} via {}", path, tmp_path);
      LOG("Renaming {} to {} failed: {}",
          path,
          tmp_path,
          rename_result.error().message());
    }
    return tl::unexpected(rename_result.error());
  }

  auto remove_result = fs::remove(tmp_path);
  if (remove_result || log_failure == LogFailure::yes) {
    LOG("Removing {} via {}", path, tmp_path);
    if (!remove_result) {
      LOG("Removal failed: {}", remove_result.error().message());
    }
  }
  return remove_result;
}

void
set_timestamps(const fs::path& path,
               std::optional<TimePoint> mtime,
               std::optional<TimePoint> atime)
{
#ifdef HAVE_UTIMENSAT
  timespec atime_mtime[2];
  if (mtime) {
    atime_mtime[0] = (atime ? *atime : *mtime).to_timespec();
    atime_mtime[1] = mtime->to_timespec();
  }
  utimensat(AT_FDCWD, path.string().c_str(), mtime ? atime_mtime : nullptr, 0);
#elif defined(HAVE_UTIMES)
  timeval atime_mtime[2];
  if (mtime) {
    atime_mtime[0].tv_sec = atime ? atime->sec() : mtime->sec();
    atime_mtime[0].tv_usec =
      (atime ? atime->nsec_decimal_part() : mtime->nsec_decimal_part()) / 1000;
    atime_mtime[1].tv_sec = mtime->sec();
    atime_mtime[1].tv_usec = mtime->nsec_decimal_part() / 1000;
  }
  utimes(path.string().c_str(), mtime ? atime_mtime : nullptr);
#else
  utimbuf atime_mtime;
  if (mtime) {
    atime_mtime.actime = atime ? atime->sec() : mtime->sec();
    atime_mtime.modtime = mtime->sec();
    utime(path.string().c_str(), &atime_mtime);
  } else {
    utime(path.string().c_str(), nullptr);
  }
#endif
}

#ifdef HAVE_DIRENT_H

tl::expected<void, std::string>
traverse_directory(const fs::path& directory,
                   const TraverseDirectoryVisitor& visitor)
{
  DIR* dir = opendir(directory.string().c_str());
  if (!dir) {
    return tl::unexpected(
      FMT("Failed to traverse {}: {}", directory, strerror(errno)));
  }

  Finalizer dir_closer([&] { closedir(dir); });

  struct dirent* entry;
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, "") == 0 || strcmp(entry->d_name, ".") == 0
        || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    auto path = directory / entry->d_name;
    bool is_dir;
#  ifdef _DIRENT_HAVE_D_TYPE
    if (entry->d_type != DT_UNKNOWN) {
      is_dir = entry->d_type == DT_DIR;
    } else
#  endif
    {
      DirEntry dir_entry(path);
      if (!dir_entry) {
        if (dir_entry.error_number() == ENOENT
            || dir_entry.error_number() == ESTALE) {
          continue;
        }
        return tl::unexpected(FMT(
          "Failed to lstat {}: {}", path, strerror(dir_entry.error_number())));
      }
      is_dir = dir_entry.is_directory();
    }
    if (is_dir) {
      traverse_directory(path, visitor);
    } else {
      visitor(path);
    }
  }
  visitor(directory);

  return {};
}

#else // If not available, use the C++17 std::filesystem implementation.

tl::expected<void, std::string>
traverse_directory(const fs::path& directory,
                   const TraverseDirectoryVisitor& visitor)
{
  // Note: Intentionally not using std::filesystem::recursive_directory_iterator
  // since it visits directories in preorder.

  DirEntry dir_entry(directory);
  if (!dir_entry.is_directory()) {
    return tl::unexpected(
      FMT("Failed to traverse {}: {}",
          directory,
          dir_entry ? "Not a directory" : "No such file or directory"));
  }

  try {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_directory()) {
        traverse_directory(entry.path(), visitor);
      } else {
        visitor(entry.path());
      }
    }
    visitor(directory);
  } catch (const std::filesystem::filesystem_error& e) {
    return tl::unexpected(e.what());
  }

  return {};
}

#endif

tl::expected<void, std::string>
write_fd(int fd, const void* data, size_t size)
{
  int64_t written = 0;
  while (static_cast<size_t>(written) < size) {
    const auto count =
      write(fd, static_cast<const uint8_t*>(data) + written, size - written);
    if (count == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        return tl::unexpected(strerror(errno));
      }
    } else {
      written += count;
    }
  }
  return {};
}

tl::expected<void, std::string>
write_file(const fs::path& path, std::string_view data, InPlace in_place)
{
  if (in_place == InPlace::no) {
    unlink(path.string().c_str());
  }
  Fd fd(
    open(path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_TEXT, 0666));
  if (!fd) {
    return tl::unexpected(strerror(errno));
  }
  return write_fd(*fd, data.data(), data.size());
}

tl::expected<void, std::string>
write_file(const fs::path& path,
           nonstd::span<const uint8_t> data,
           InPlace in_place)
{
  if (in_place == InPlace::no) {
    unlink(path.string().c_str());
  }
  Fd fd(
    open(path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
  if (!fd) {
    return tl::unexpected(strerror(errno));
  }
  return write_fd(*fd, data.data(), data.size());
}

} // namespace util
