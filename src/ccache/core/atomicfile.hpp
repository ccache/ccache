// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include <nonstd/span.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string_view>

namespace core {

// This class represents a file whose data will be atomically written to a path
// by renaming a temporary file in place. Throws core::Error on error.
class AtomicFile
{
public:
  enum class Mode { binary, text };

  AtomicFile(const std::filesystem::path& path, Mode mode);
  ~AtomicFile();

  FILE* stream();

  void write(std::string_view data);
  void write(nonstd::span<const uint8_t> data);
  void flush();

  // Close the temporary file and rename it to the destination file. Note: The
  // destructor will not do this automatically to avoid half-written data in the
  // file.
  void commit();

private:
  std::filesystem::path m_path;
  std::filesystem::path m_tmp_path;
  FILE* m_stream;
};

inline FILE*
AtomicFile::stream()
{
  return m_stream;
}

} // namespace core
