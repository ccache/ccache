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

#include "Stat.hpp"
#include "exceptions.hpp"

#include "third_party/nonstd/optional.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

class CacheFile
{
public:
  enum class Type { result, manifest, unknown };

  explicit CacheFile(const std::string& path);

  CacheFile(const CacheFile&) = delete;
  CacheFile& operator=(const CacheFile&) = delete;

  const Stat& lstat() const;
  const std::string& path() const;
  Type type() const;

private:
  const std::string m_path;
  mutable nonstd::optional<Stat> m_stat;
};

inline CacheFile::CacheFile(const std::string& path) : m_path(path)
{
}

inline const std::string&
CacheFile::path() const
{
  return m_path;
}
