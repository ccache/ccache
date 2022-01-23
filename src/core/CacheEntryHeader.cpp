// Copyright (C) 2019-2022 Joel Rosdahl and other contributors
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

#include "CacheEntryHeader.hpp"

#include <fmtmacros.hpp>

const size_t k_static_header_fields_size =
  sizeof(core::CacheEntryHeader::magic)
  + sizeof(core::CacheEntryHeader::entry_format_version)
  + sizeof(core::CacheEntryHeader::entry_type)
  + sizeof(core::CacheEntryHeader::compression_type)
  + sizeof(core::CacheEntryHeader::compression_level)
  + sizeof(core::CacheEntryHeader::creation_time)
  + sizeof(core::CacheEntryHeader::entry_size)
  // ccache_version length field:
  + 1
  // namespace_ length field:
  + 1;

const size_t k_static_epilogue_fields_size =
  sizeof(uint64_t) + sizeof(uint64_t);

namespace core {

CacheEntryHeader::CacheEntryHeader(const core::CacheEntryType entry_type_,
                                   const compression::Type compression_type_,
                                   const int8_t compression_level_,
                                   const uint64_t creation_time_,
                                   const std::string& ccache_version_,
                                   const std::string& namespace_arg,
                                   const uint64_t entry_size_)
  : magic(k_ccache_magic),
    entry_format_version(k_entry_format_version),
    entry_type(entry_type_),
    compression_type(compression_type_),
    compression_level(compression_level_),
    creation_time(creation_time_),
    ccache_version(ccache_version_),
    namespace_(namespace_arg),
    entry_size(entry_size_)
{
}

uint64_t
CacheEntryHeader::payload_size() const
{
  return entry_size - non_payload_size();
}

void
CacheEntryHeader::set_entry_size_from_payload_size(const uint64_t payload_size)
{
  entry_size = non_payload_size() + payload_size;
}

void
CacheEntryHeader::dump(FILE* const stream) const
{
  PRINT(stream, "Magic: {:04x}\n", magic);
  PRINT(stream, "Entry format version: {}\n", entry_format_version);
  PRINT(stream,
        "Entry type: {} ({})\n",
        static_cast<uint8_t>(entry_type),
        to_string(entry_type));
  PRINT(stream,
        "Compression type: {}\n",
        compression::type_to_string(compression_type));
  PRINT(stream, "Compression level: {}\n", compression_level);
  PRINT(stream, "Creation time: {}\n", creation_time);
  PRINT(stream, "Ccache version: {}\n", ccache_version);
  PRINT(stream, "Namespace: {}\n", namespace_);
  PRINT(stream, "Entry size: {}\n", entry_size);
}

size_t
CacheEntryHeader::non_payload_size() const
{
  return k_static_header_fields_size + ccache_version.length()
         + namespace_.length() + k_static_epilogue_fields_size;
}

} // namespace core
