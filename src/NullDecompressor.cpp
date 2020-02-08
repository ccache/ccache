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

#include "NullDecompressor.hpp"

#include "exceptions.hpp"

NullDecompressor::NullDecompressor(FILE* stream) : m_stream(stream)
{
}

void
NullDecompressor::read(void* data, size_t count)
{
  if (fread(data, count, 1, m_stream) != 1) {
    throw Error("failed to read from uncompressed stream");
  }
}

void
NullDecompressor::finalize()
{
  if (fgetc(m_stream) != EOF) {
    throw Error("garbage data at end of uncompressed stream");
  }
}
