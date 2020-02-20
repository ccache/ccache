// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "system.hpp"

#include "exceptions.hpp"

#include <string>

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

protected:
  using StatFunction = int (*)(const char*, struct stat*);

  Stat(StatFunction stat_function, const std::string& path, OnError on_error);

private:
  struct stat m_stat;
  int m_errno;

  bool operator==(const Stat&) const;
  bool operator!=(const Stat&) const;
};

inline Stat::Stat() : m_stat{}, m_errno(-1)
{
}

inline Stat
Stat::stat(const std::string& path, OnError on_error)
{
  return Stat(::stat, path, on_error);
}

inline Stat
Stat::lstat(const std::string& path, OnError on_error)
{
  return Stat(
#ifdef _WIN32
    ::stat,
#else
    ::lstat,
#endif
    path,
    on_error);
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

inline dev_t
Stat::device() const
{
  return m_stat.st_dev;
}

inline ino_t
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
  return m_stat.st_ctime;
}

inline time_t
Stat::mtime() const
{
  return m_stat.st_mtime;
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
#ifndef _WIN32
  return S_ISLNK(mode());
#else
  return false;
#endif
}

inline bool
Stat::is_regular() const
{
  return S_ISREG(mode());
}
