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

#include <compression/Decompressor.hpp>
#include <core/CacheEntryHeader.hpp>
#include <core/ChecksummingReader.hpp>
#include <core/Reader.hpp>
#include <util/XXH3_128.hpp>

namespace core {

// This class knows how to read a cache entry with a format described in
// CacheEntryHeader.
class CacheEntryReader : public Reader
{
public:
  // Read cache entry data from `reader`.
  CacheEntryReader(Reader& reader);

  size_t read(void* data, size_t count) override;
  using Reader::read;

  // Close for reading.
  //
  // This method potentially verifies the end state after reading the cache
  // entry and throws `core::Error` if any integrity issues are found.
  void finalize();

  const CacheEntryHeader& header() const;

private:
  ChecksummingReader m_checksumming_reader;
  std::unique_ptr<CacheEntryHeader> m_header;
  util::XXH3_128 m_checksum;
  std::unique_ptr<compression::Decompressor> m_decompressor;
};

inline const CacheEntryHeader&
CacheEntryReader::header() const
{
  return *m_header;
}

} // namespace core
