// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "Error.hpp"

#include <cerrno>
#include <cstring>
#include <fmt/core.h>
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

  const std::string& path() const;
  const struct stat& stat() const;
  Type type() const;

private:
  const std::string m_path;
  mutable struct stat m_stat;
  mutable bool m_stated = false;
};

inline CacheFile::CacheFile(const std::string& path) : m_path(path)
{
}

inline const std::string&
CacheFile::path() const
{
  return m_path;
}
