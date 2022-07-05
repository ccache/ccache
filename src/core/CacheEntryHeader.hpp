// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include <compression/types.hpp>
#include <core/types.hpp>

#include <cstdint>

// Cache entry format
// ==================
//
// Integers are big-endian.
//
// <entry>            ::= <header> <payload> <epilogue>
// <header>           ::= <magic> <format_ver> <entry_type> <compr_type>
//                        <compr_level> <creation_time> <ccache_ver> <namespace>
//                        <entry_size>
// <magic>            ::= uint16_t (0xccac)
// <format_ver>       ::= uint8_t
// <entry_type>       ::= <result_entry> | <manifest_entry>
// <result_entry>     ::= 0 (uint8_t)
// <manifest_entry>   ::= 1 (uint8_t)
// <compr_type>       ::= <compr_none> | <compr_zstd>
// <compr_none>       ::= 0 (uint8_t)
// <compr_zstd>       ::= 1 (uint8_t)
// <compr_level>      ::= int8_t
// <creation_time>    ::= uint64_t (Unix epoch time when entry was created)
// <ccache_ver>       ::= string length (uint8_t) + string data
// <namespace>        ::= string length (uint8_t) + string data
// <entry_size>       ::= uint64_t ; = size of file if stored uncompressed
// ; potentially compressed from here
// <payload>          ::= depends on entry_type
// <epilogue>         ::= <checksum_high> <checksum_low>
// <checksum_high>    ::= uint64_t ; XXH3-128 (high bits) of entry bytes
// <checksum_low>     ::= uint64_t ; XXH3-128 (low bits) of entry bytes

namespace core {

const uint16_t k_ccache_magic = 0xccac;
const uint16_t k_entry_format_version = 0;

struct CacheEntryHeader
{
  CacheEntryHeader(core::CacheEntryType entry_type,
                   compression::Type compression_type,
                   int8_t compression_level,
                   uint64_t creation_time,
                   const std::string& ccache_version,
                   const std::string& namespace_,
                   uint64_t entry_size = 0);

  uint16_t magic;
  uint8_t entry_format_version;
  core::CacheEntryType entry_type;
  compression::Type compression_type;
  int8_t compression_level;
  uint64_t creation_time;
  std::string ccache_version;
  std::string namespace_;
  uint64_t entry_size;

  uint64_t payload_size() const;
  void set_entry_size_from_payload_size(uint64_t payload_size);
  void inspect(FILE* stream) const;

private:
  size_t non_payload_size() const;
};

} // namespace core
