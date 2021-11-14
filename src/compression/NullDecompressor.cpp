// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include "NullDecompressor.hpp"

#include <core/exceptions.hpp>

namespace compression {

NullDecompressor::NullDecompressor(core::Reader& reader) : m_reader(reader)
{
}

size_t
NullDecompressor::read(void* const data, const size_t count)
{
  return m_reader.read(data, count);
}

void
NullDecompressor::finalize()
{
  bool eof;
  try {
    m_reader.read_int<uint8_t>();
    eof = false;
  } catch (core::Error&) {
    eof = true;
  }
  if (!eof) {
    throw core::Error("Garbage data at end of uncompressed stream");
  }
}

} // namespace compression
