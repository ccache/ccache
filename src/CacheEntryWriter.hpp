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

#pragma once

#include "Checksum.hpp"
#include "Util.hpp"

#include <compression/Compressor.hpp>

#include <cstdint>
#include <cstdio>
#include <memory>

// This class knows how to write a cache entry with a common header and a
// payload part that is different depending on the cache entry type (result or
// manifest).
class CacheEntryWriter
{
public:
  // Constructor.
  //
  // Parameters:
  // - stream: Stream to write header + payload to.
  // - magic: File format magic (first four bytes of the file).
  // - version: File format version.
  // - compression_type: Compression type to use.
  // - compression_level: Compression level to use.
  // - payload_size: Payload size.
  CacheEntryWriter(FILE* stream,
                   const uint8_t* magic,
                   uint8_t version,
                   compression::Type compression_type,
                   int8_t compression_level,
                   uint64_t payload_size);

  // Write data to the payload from a buffer.
  //
  // Parameters:
  // - data: Data to write.
  // - count: Size of data to write.
  //
  // Throws Error on failure.
  void write(const void* data, size_t count);

  // Write an unsigned integer to the payload.
  //
  // Parameters:
  // - value: Value to write.
  //
  // Throws Error on failure.
  template<typename T> void write(T value);

  // Close for writing.
  //
  // This method potentially verifies the end state after writing the cache
  // entry and throws Error if any integrity issues are found.
  void finalize();

private:
  std::unique_ptr<compression::Compressor> m_compressor;
  Checksum m_checksum;
};

template<typename T>
inline void
CacheEntryWriter::write(T value)
{
  uint8_t buffer[sizeof(T)];
  Util::int_to_big_endian(value, buffer);
  write(buffer, sizeof(T));
}
