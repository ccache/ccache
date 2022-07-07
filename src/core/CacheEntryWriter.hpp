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

#include <compression/Compressor.hpp>
#include <core/ChecksummingWriter.hpp>
#include <core/Writer.hpp>

namespace core {

struct CacheEntryHeader;

// This class knows how to write a cache entry with a format described in
// CacheEntryHeader.
class CacheEntryWriter : public Writer
{
public:
  CacheEntryWriter(Writer& writer,
                   const std::string dict_dir,
                   const CacheEntryHeader& header);

  void write(const void* data, size_t count) override;
  using Writer::write;

  // Close for writing.
  //
  // This method potentially verifies the end state after writing the cache
  // entry and throws `core::Error` if any integrity issues are found.
  void finalize() override;

private:
  ChecksummingWriter m_checksumming_writer;
  std::unique_ptr<compression::Compressor> m_compressor;
};

} // namespace core
