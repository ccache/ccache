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

#include "CacheEntryWriter.hpp"

#include <core/CacheEntryHeader.hpp>

namespace core {

CacheEntryWriter::CacheEntryWriter(core::Writer& writer,
                                   const CacheEntryHeader& header)
  : m_checksumming_writer(writer),
    m_compressor(compression::Compressor::create_from_type(
      header.compression_type, writer, header.compression_level))
{
  m_checksumming_writer.write_int(header.magic);
  m_checksumming_writer.write_int(header.entry_format_version);
  m_checksumming_writer.write_int(static_cast<uint8_t>(header.entry_type));
  m_checksumming_writer.write_int(
    static_cast<uint8_t>(header.compression_type));
  m_checksumming_writer.write_int(header.compression_level);
  m_checksumming_writer.write_int(header.creation_time);
  m_checksumming_writer.write_int<uint8_t>(header.ccache_version.length());
  m_checksumming_writer.write_str(header.ccache_version);
  m_checksumming_writer.write_int<uint8_t>(header.namespace_.length());
  m_checksumming_writer.write_str(header.namespace_);
  m_checksumming_writer.write_int(header.entry_size);

  m_checksumming_writer.set_writer(*m_compressor);
}

void
CacheEntryWriter::write(const void* const data, const size_t count)
{
  m_checksumming_writer.write(data, count);
}

void
CacheEntryWriter::finalize()
{
  const auto digest = m_checksumming_writer.digest();
  m_compressor->write(digest.bytes(), digest.size());
  m_compressor->finalize();
}

} // namespace core
