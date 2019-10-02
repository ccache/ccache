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

#pragma once

#include "Decompressor.hpp"
#include "Util.hpp"

#include <cstdio>
#include <memory>

class Checksum;

// This class knows how to read a cache entry with a common header and a
// payload part that is different depending on the cache entry type (result or
// manifest).
class CacheEntryReader
{
public:
  // Constructor.
  //
  // Parameters:
  // - stream: Stream to read header and payload from.
  // - expected_magic: Expected magic bytes (first four bytes of the file).
  // - expected_version: Expected file format version.
  // - checksum: Checksum state that will be updated with the read bytes, or
  //   nullptr for no checksumming.
  CacheEntryReader(FILE* stream,
                   const uint8_t expected_magic[4],
                   uint8_t expected_version,
                   Checksum* checksum = nullptr);

  // Dump header information in text format.
  //
  // Parameters:
  // - dump_stream: Stream to write to.
  void dump_header(FILE* dump_stream);

  // Read data into a buffer from the payload.
  //
  // Parameters:
  // - data: Buffer to write data to.
  // - count: How many bytes to write.
  //
  // Throws Error on failure.
  void read(void* data, size_t count);

  // Read an unsigned integer from the payload.
  //
  // Parameters:
  // - value: Variable to write to.
  //
  // Throws Error on failure.
  template<typename T> void read(T& value);

  // Close for reading.
  //
  // This method potentially verifies the end state after reading the cache
  // entry and throws Error if any integrity issues are found.
  void finalize();

  // Get size of the content (header + payload).
  uint64_t content_size() const;

private:
  std::unique_ptr<Decompressor> m_decompressor;
  Checksum* m_checksum;
  char m_magic[4];
  uint8_t m_version;
  Compression::Type m_compression_type;
  int8_t m_compression_level;
  uint64_t m_content_size;
};

template<typename T>
inline void
CacheEntryReader::read(T& value)
{
  uint8_t buffer[sizeof(T)];
  read(buffer, sizeof(T));
  Util::big_endian_to_int(buffer, value);
}

inline uint64_t
CacheEntryReader::content_size() const
{
  return m_content_size;
}
