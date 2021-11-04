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

#include <core/Writer.hpp>
#include <core/exceptions.hpp>

#include <cstdio>

namespace core {

class FileWriter : public Writer
{
public:
  FileWriter(FILE* stream);

  void write(const void* data, size_t size) override;
  void finalize() override;

private:
  FILE* m_stream;
};

inline FileWriter::FileWriter(FILE* const stream) : m_stream(stream)
{
}

inline void
FileWriter::write(const void* const data, const size_t size)
{
  if (size > 0 && fwrite(data, size, 1, m_stream) != 1) {
    throw core::Error("Failed to write to stream");
  }
}

inline void
FileWriter::finalize()
{
  fflush(m_stream);
}

} // namespace core
