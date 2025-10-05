// Copyright (C) 2019-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/time.hpp>
#include <ccache/util/wincompat.hpp>

#include <sys/stat.h>

#include <cstdint>
#include <filesystem>

namespace util {

// This class is similar to std::filesystem::directory_entry with a couple of
// extra features, for example:
//
// - operator bool tells whether the directory entry exists (not following
//   symlinks, in contrast to the exists() method).
// - Supports access to atime and ctime fields.
// - Supports logging on error.
class DirEntry
{
public:
  enum class LogOnError : bool { no, yes };

#if defined(_WIN32)
  struct stat_t
  {
    uint64_t st_dev;
    uint64_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint64_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    uint32_t st_file_attributes;
    uint32_t st_reparse_tag;
  };
#else
  using stat_t = struct stat;
#endif

  using dev_t = decltype(stat_t{}.st_dev);
  using ino_t = decltype(stat_t{}.st_ino);

  // Create an empty directory entry. operator bool() will return false,
  // error_number() will return ENOENT and other accessors will return false or
  // 0.
  DirEntry() = default;

  // Stat a path.
  //
  // The underlying (l)stat(2) call will not be made by the constructor but
  // on-demand when calling the first query function. That (l)stat result is
  // then cached. See also the refresh method.
  DirEntry(const std::filesystem::path& path,
           LogOnError log_on_error = LogOnError::no);

#ifndef _WIN32
  // Stat an open file descriptor.
  //
  // The underlying fstat(2) call will not be made by the constructor but
  // on-demand when calling the first query function. That fstat result is then
  // cached. See also the refresh method.
  DirEntry(const std::filesystem::path& path,
           int fd,
           LogOnError log_on_error = LogOnError::no);
#endif

  // Return true if the file could be lstat(2)-ed (i.e., the directory entry
  // exists without following symlinks), otherwise false.
  operator bool() const;

  // Return true if the file could be stat(2)-ed (i.e., the directory entry
  // exists when following symlinks), otherwise false.
  bool exists() const;

  // Return the path that this entry refers to.
  const std::filesystem::path& path() const;

  // Return whether the entry refers to the same device and i-node as `other`.
  bool same_inode_as(const DirEntry& other) const;

  // Return errno from the lstat(2) call (0 if successful).
  int error_number() const;

  dev_t device() const;
  ino_t inode() const;
  mode_t mode() const;
  util::TimePoint atime() const;
  util::TimePoint ctime() const;
  util::TimePoint mtime() const;
  uint64_t size() const;

  uint64_t size_on_disk() const;

  bool is_directory() const;
  bool is_regular_file() const;
  bool is_symlink() const;

  // Update the cached (l)stat(2) result.
  void refresh();

#ifdef _WIN32
  uint32_t file_attributes() const;
  uint32_t reparse_tag() const;
#endif

private:
  std::filesystem::path m_path;
#ifndef _WIN32
  int m_fd = -1;
#endif
  LogOnError m_log_on_error = LogOnError::no;
  mutable stat_t m_stat;
  mutable int m_errno = -1;
  mutable bool m_initialized = false;
  mutable bool m_exists = false;
  mutable bool m_is_symlink = false;

  const stat_t& do_stat() const;
};

inline DirEntry::DirEntry(const std::filesystem::path& path,
                          LogOnError log_on_error)
  : m_path(path),
    m_log_on_error(log_on_error)
{
}

#ifndef _WIN32
inline DirEntry::DirEntry(const std::filesystem::path& path,
                          int fd,
                          LogOnError log_on_error)
  : m_path(path),
    m_fd(fd),
    m_log_on_error(log_on_error)
{
}
#endif

inline DirEntry::operator bool() const
{
  do_stat();
  return m_errno == 0;
}

inline bool
DirEntry::exists() const
{
  do_stat();
  return m_exists;
}

inline bool
DirEntry::same_inode_as(const DirEntry& other) const
{
  do_stat();
  return m_errno == 0 && device() == other.device() && inode() == other.inode();
}

inline const std::filesystem::path&
DirEntry::path() const
{
  return m_path;
}

inline int
DirEntry::error_number() const
{
  do_stat();
  return m_errno;
}

inline DirEntry::dev_t
DirEntry::device() const
{
  return do_stat().st_dev;
}

inline DirEntry::ino_t
DirEntry::inode() const
{
  return do_stat().st_ino;
}

inline mode_t
DirEntry::mode() const
{
  return do_stat().st_mode;
}

inline util::TimePoint
DirEntry::atime() const
{
#if defined(_WIN32) || defined(HAVE_STRUCT_STAT_ST_ATIM)
  return util::timepoint_from_timespec(do_stat().st_atim);
#elif defined(HAVE_STRUCT_STAT_ST_ATIMESPEC)
  return util::timepoint_from_timespec(do_stat().st_atimespec);
#elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
  return util::timepoint_from_sec_nsec(do_stat().st_atime,
                                       do_stat().st_atimensec);
#else
  return util::timepoint_from_sec_nsec(do_stat().st_atime, 0);
#endif
}

inline util::TimePoint
DirEntry::ctime() const
{
#if defined(_WIN32) || defined(HAVE_STRUCT_STAT_ST_CTIM)
  return util::timepoint_from_timespec(do_stat().st_ctim);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC)
  return util::timepoint_from_timespec(do_stat().st_ctimespec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
  return util::timepoint_from_sec_nsec(do_stat().st_ctime,
                                       do_stat().st_ctimensec);
#else
  return util::timepoint_from_sec_nsec(do_stat().st_ctime, 0);
#endif
}

inline util::TimePoint
DirEntry::mtime() const
{
#if defined(_WIN32) || defined(HAVE_STRUCT_STAT_ST_MTIM)
  return util::timepoint_from_timespec(do_stat().st_mtim);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
  return util::timepoint_from_timespec(do_stat().st_mtimespec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
  return util::timepoint_from_sec_nsec(do_stat().st_mtime,
                                       do_stat().st_mtimensec);
#else
  return util::timepoint_from_sec_nsec(do_stat().st_mtime, 0);
#endif
}

inline uint64_t
DirEntry::size() const
{
  return do_stat().st_size;
}

inline bool
DirEntry::is_directory() const
{
  return S_ISDIR(mode());
}

inline bool
DirEntry::is_symlink() const
{
  return m_is_symlink;
}

inline void
DirEntry::refresh()
{
  m_initialized = false;
  do_stat();
}

inline bool
DirEntry::is_regular_file() const
{
  return S_ISREG(mode());
}

#ifdef _WIN32
inline uint32_t
DirEntry::file_attributes() const
{
  return do_stat().st_file_attributes;
}

inline uint32_t
DirEntry::reparse_tag() const
{
  return do_stat().st_reparse_tag;
}
#endif

} // namespace util
