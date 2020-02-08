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

#include "CacheEntryReader.hpp"

#include "Compressor.hpp"
#include "exceptions.hpp"

#include "third_party/fmt/core.h"

CacheEntryReader::CacheEntryReader(FILE* stream,
                                   const uint8_t expected_magic[4],
                                   uint8_t expected_version)
{
  uint8_t header_bytes[15];
  if (fread(header_bytes, sizeof(header_bytes), 1, stream) != 1) {
    throw Error("Error reading header");
  }

  memcpy(m_magic, header_bytes, sizeof(m_magic));
  m_version = header_bytes[4];
  m_compression_type = Compression::type_from_int(header_bytes[5]);
  m_compression_level = header_bytes[6];
  Util::big_endian_to_int(header_bytes + 7, m_content_size);

  if (memcmp(m_magic, expected_magic, sizeof(m_magic)) != 0) {
    throw Error(fmt::format("Bad magic value 0x{:02x}{:02x}{:02x}{:02x}",
                            m_magic[0],
                            m_magic[1],
                            m_magic[2],
                            m_magic[3]));
  }
  if (m_version != expected_version) {
    throw Error(fmt::format(
      "Unknown version (actual {}, expected {})", m_version, expected_version));
  }

  m_checksum.update(header_bytes, sizeof(header_bytes));
  m_decompressor = Decompressor::create_from_type(m_compression_type, stream);
}

void
CacheEntryReader::dump_header(FILE* dump_stream)
{
  fmt::print(dump_stream, "Magic: {:.4}\n", m_magic);
  fmt::print(dump_stream, "Version: {}\n", m_version);
  fmt::print(dump_stream,
             "Compression type: {}\n",
             Compression::type_to_string(m_compression_type));
  fmt::print(dump_stream, "Compression level: {}\n", m_compression_level);
  fmt::print(dump_stream, "Content size: {}\n", m_content_size);
}

void
CacheEntryReader::read(void* data, size_t count)
{
  m_decompressor->read(data, count);
  m_checksum.update(data, count);
}

void
CacheEntryReader::finalize()
{
  uint64_t actual_digest = m_checksum.digest();

  uint8_t buffer[8];
  read(buffer, sizeof(buffer));
  uint64_t expected_digest;
  Util::big_endian_to_int(buffer, expected_digest);

  if (actual_digest != expected_digest) {
    throw Error(
      fmt::format("Incorrect checksum (actual 0x{:016x}, expected 0x{:016x})",
                  actual_digest,
                  expected_digest));
  }

  m_decompressor->finalize();
}
