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

#include <core/Reader.hpp>
#include <core/exceptions.hpp>

#include <cstdio>

namespace core {

class FileReader : public Reader
{
public:
  FileReader(FILE* stream);

  size_t read(void* data, size_t size) override;

private:
  FILE* m_stream;
};

inline FileReader::FileReader(FILE* stream) : m_stream(stream)
{
}

inline size_t
FileReader::read(void* const data, const size_t size)
{
  if (size == 0) {
    return 0;
  }
  const auto bytes_read = fread(data, 1, size, m_stream);
  if (bytes_read == 0) {
    throw core::Error("Failed to read from file stream");
  }
  return bytes_read;
}

} // namespace core
