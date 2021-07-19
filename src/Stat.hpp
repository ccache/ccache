// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#pragma once

#include <core/wincompat.hpp>

#include <sys/stat.h>
#include <sys/types.h>

#include <ctime>
#include <string>

#ifdef _WIN32
#  ifndef S_IFIFO
#    define S_IFIFO 0x1000
#  endif
#  ifndef S_IFBLK
#    define S_IFBLK 0x6000
#  endif
#  ifndef S_IFLNK
#    define S_IFLNK 0xA000
#  endif
#  ifndef S_ISREG
#    define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#  endif
#  ifndef S_ISDIR
#    define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#  endif
#  ifndef S_ISFIFO
#    define S_ISFIFO(m) (((m)&S_IFMT) == S_IFIFO)
#  endif
#  ifndef S_ISCHR
#    define S_ISCHR(m) (((m)&S_IFMT) == S_IFCHR)
#  endif
#  ifndef S_ISLNK
#    define S_ISLNK(m) (((m)&S_IFMT) == S_IFLNK)
#  endif
#  ifndef S_ISBLK
#    define S_ISBLK(m) (((m)&S_IFMT) == S_IFBLK)
#  endif
#endif

class Stat
{
public:
  enum class OnError {
    // Ignore any error (including missing file) from the underlying stat call.
    // On error, error_number() will return the error number (AKA errno) and
    // the query functions will return 0 or false.
    ignore,
    // Like above but log an error message as well.
    log,
    // Throw Error on errors (including missing file).
    throw_error,
  };

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
  // Use of typedef needed to suppress a spurious 'declaration does not declare
  // anything' warning in old GCC.
  typedef struct ::stat stat_t; // NOLINT(modernize-use-using)
#endif

  using dev_t = decltype(stat_t{}.st_dev);
  using ino_t = decltype(stat_t{}.st_ino);

  // Create an empty stat result. operator bool() will return false,
  // error_number() will return -1 and other accessors will return false or 0.
  Stat();

  // Run stat(2).
  //
  // Arguments:
  // - path: Path to stat.
  // - on_error: What to do on errors (including missing file).
  static Stat stat(const std::string& path, OnError on_error = OnError::ignore);

  // Run lstat(2) if available, otherwise stat(2).
  //
  // Arguments:
  // - path: Path to (l)stat.
  // - on_error: What to do on errors (including missing file).
  static Stat lstat(const std::string& path,
                    OnError on_error = OnError::ignore);

  // Return true if the file could be (l)stat-ed (i.e., the file exists),
  // otherwise false.
  operator bool() const;

  // Return whether this object refers to the same device and i-node as `other`
  // does.
  bool same_inode_as(const Stat& other) const;

  // Return errno from the (l)stat call (0 if successful).
  int error_number() const;

  dev_t device() const;
  ino_t inode() const;
  mode_t mode() const;
  time_t ctime() const;
  time_t mtime() const;
  uint64_t size() const;

  uint64_t size_on_disk() const;

  bool is_directory() const;
  bool is_regular() const;
  bool is_symlink() const;

#ifdef _WIN32
  uint32_t file_attributes() const;
  uint32_t reparse_tag() const;
#endif

  timespec ctim() const;
  timespec mtim() const;

protected:
  using StatFunction = int (*)(const char*, stat_t*);

  Stat(StatFunction stat_function, const std::string& path, OnError on_error);

private:
  stat_t m_stat;
  int m_errno;

  bool operator==(const Stat&) const;
  bool operator!=(const Stat&) const;
};

inline Stat::Stat() : m_stat{}, m_errno(-1)
{
}

inline Stat::operator bool() const
{
  return m_errno == 0;
}

inline bool
Stat::same_inode_as(const Stat& other) const
{
  return device() == other.device() && inode() == other.inode();
}

inline int
Stat::error_number() const
{
  return m_errno;
}

inline Stat::dev_t
Stat::device() const
{
  return m_stat.st_dev;
}

inline Stat::ino_t
Stat::inode() const
{
  return m_stat.st_ino;
}

inline mode_t
Stat::mode() const
{
  return m_stat.st_mode;
}

inline time_t
Stat::ctime() const
{
  return ctim().tv_sec;
}

inline time_t
Stat::mtime() const
{
  return mtim().tv_sec;
}

inline uint64_t
Stat::size() const
{
  return m_stat.st_size;
}

inline uint64_t
Stat::size_on_disk() const
{
#ifdef _WIN32
  return (size() + 1023) & ~1023;
#else
  return m_stat.st_blocks * 512;
#endif
}

inline bool
Stat::is_directory() const
{
  return S_ISDIR(mode());
}

inline bool
Stat::is_symlink() const
{
  return S_ISLNK(mode());
}

inline bool
Stat::is_regular() const
{
  return S_ISREG(mode());
}

#ifdef _WIN32
inline uint32_t
Stat::file_attributes() const
{
  return m_stat.st_file_attributes;
}

inline uint32_t
Stat::reparse_tag() const
{
  return m_stat.st_reparse_tag;
}
#endif

inline timespec
Stat::ctim() const
{
#if defined(_WIN32) || defined(HAVE_STRUCT_STAT_ST_CTIM)
  return m_stat.st_ctim;
#else
  return {m_stat.st_ctime, 0};
#endif
}

inline timespec
Stat::mtim() const
{
#if defined(_WIN32) || defined(HAVE_STRUCT_STAT_ST_MTIM)
  return m_stat.st_mtim;
#else
  return {m_stat.st_mtime, 0};
#endif
}
