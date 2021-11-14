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

#include "NullCompressor.hpp"

#include <core/exceptions.hpp>

namespace compression {

NullCompressor::NullCompressor(core::Writer& writer) : m_writer(writer)
{
}

int8_t
NullCompressor::actual_compression_level() const
{
  return 0;
}

void
NullCompressor::write(const void* const data, const size_t count)
{
  m_writer.write(data, count);
}

void
NullCompressor::finalize()
{
  m_writer.finalize();
}

} // namespace compression
