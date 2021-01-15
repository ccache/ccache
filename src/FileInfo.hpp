// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include "Stat.hpp"

#include "third_party/nonstd/optional.hpp"

#include <string>

// file path and cached lstat
class FileInfo
{
public:
  explicit FileInfo(const std::string& path);

  const std::string& path() const;

  bool exists() const;
  bool is_directory() const;
  bool is_regular() const;
  uint64_t size_on_disk() const;
  uint64_t size() const;
  time_t mtime() const;

private:
  const Stat& lstat() const;

  std::string m_path;
  mutable nonstd::optional<Stat> m_stat;
};

inline FileInfo::FileInfo(const std::string& path) : m_path(path)
{
}

inline const std::string&
FileInfo::path() const
{
  return m_path;
}

inline bool
FileInfo::exists() const
{
  return lstat();
}

inline bool
FileInfo::is_directory() const
{
  return lstat() && lstat().is_directory();
}

inline bool
FileInfo::is_regular() const
{
  return lstat() && lstat().is_regular();
}

inline uint64_t
FileInfo::size_on_disk() const
{
  return lstat().size_on_disk();
}

inline uint64_t
FileInfo::size() const
{
  return lstat().size();
}

inline time_t
FileInfo::mtime() const
{
  return lstat().mtime();
}
