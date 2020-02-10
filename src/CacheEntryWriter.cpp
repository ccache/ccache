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

#include "CacheEntryWriter.hpp"

CacheEntryWriter::CacheEntryWriter(FILE* stream,
                                   const uint8_t magic[4],
                                   uint8_t version,
                                   Compression::Type compression_type,
                                   int8_t compression_level,
                                   uint64_t payload_size)
  // clang-format off
  : m_compressor(
      Compressor::create_from_type(compression_type, stream, compression_level)
    )
{
  // clang-format on
  uint8_t header_bytes[15];
  memcpy(header_bytes, magic, 4);
  header_bytes[4] = version;
  header_bytes[5] = static_cast<uint8_t>(compression_type);
  header_bytes[6] = m_compressor->actual_compression_level();
  uint64_t content_size = 15 + payload_size + 8;
  Util::int_to_big_endian(content_size, header_bytes + 7);
  if (fwrite(header_bytes, sizeof(header_bytes), 1, stream) != 1) {
    throw Error("Failed to write cache entry header");
  }
  m_checksum.update(header_bytes, sizeof(header_bytes));
}

void
CacheEntryWriter::write(const void* data, size_t count)
{
  m_compressor->write(data, count);
  m_checksum.update(data, count);
}

void
CacheEntryWriter::finalize()
{
  uint8_t buffer[8];
  Util::int_to_big_endian(m_checksum.digest(), buffer);
  m_compressor->write(buffer, sizeof(buffer));
  m_compressor->finalize();
}
