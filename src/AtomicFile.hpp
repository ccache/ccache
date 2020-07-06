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

#include <string>
#include <vector>

// This class represents a file whose data will be atomically written to a path
// by renaming a temporary file in place.
class AtomicFile
{
public:
  enum class Mode { binary, text };

  AtomicFile(const std::string& path, Mode mode);
  ~AtomicFile();

  FILE* stream();

  void write(const std::string& data);
  void write(const std::vector<uint8_t>& data);

  // Close the temporary file and rename it to the destination file. Note: The
  // destructor will not do this automatically to avoid half-written data in the
  // file.
  void commit();

private:
  const std::string m_path;
  std::string m_tmp_path;
  FILE* m_stream;
};

inline FILE*
AtomicFile::stream()
{
  return m_stream;
}
