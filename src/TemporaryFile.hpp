// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "Fd.hpp"

#include <string>
#include <string_view>

// This class represents a unique temporary file created by mkstemp. The file is
// not deleted by the destructor.
class TemporaryFile
{
public:
  static constexpr char tmp_file_infix[] = ".tmp.";

  // `path_prefix` is the base path. The resulting filename will be this path
  // plus a unique string plus `suffix`. If `path_prefix` refers to a
  // nonexistent directory the directory will be created if possible.
  TemporaryFile(std::string_view path_prefix, std::string_view suffix = ".tmp");

  TemporaryFile(TemporaryFile&& other) noexcept = default;

  TemporaryFile& operator=(TemporaryFile&& other) noexcept = default;

  static bool is_tmp_file(std::string_view path);

  // The resulting open file descriptor in read/write mode. Unset on error.
  Fd fd;

  // The actual filename. Empty on error.
  std::string path;
};
