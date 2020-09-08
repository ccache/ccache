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

#pragma once

#include "Fd.hpp"

#include "third_party/nonstd/string_view.hpp"

// This class represents a unique temporary file created by mkstemp. The file is
// not deleted by the destructor.
class TemporaryFile
{
public:
  // `path_prefix` is the base path. The resulting filename will be this path
  //  plus a unique suffix. If `path_prefix` refers to a nonexistent directory
  //  the directory will be created if possible.`
  TemporaryFile(nonstd::string_view path_prefix);

  TemporaryFile(TemporaryFile&& other) noexcept = default;

  TemporaryFile& operator=(TemporaryFile&& other) noexcept = default;

  // The resulting open file descriptor in read/write mode. Unset on error.
  Fd fd;

  // The actual filename. Empty on error.
  std::string path;

private:
  bool initialize(nonstd::string_view path_prefix);
};
