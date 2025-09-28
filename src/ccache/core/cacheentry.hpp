// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include <ccache/core/serializer.hpp>
#include <ccache/core/types.hpp>
#include <ccache/util/bytes.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

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
// <self_contained>   ::= 0/1 (uint8_t) ; whether suitable for remote storage
// <compr_type>       ::= <compr_none> | <compr_zstd>
// <compr_none>       ::= 0 (uint8_t)
// <compr_zstd>       ::= 1 (uint8_t)
// <compr_level>      ::= int8_t
// <creation_time>    ::= uint64_t (Unix epoch time when entry was created)
// <ccache_ver>       ::= string length (uint8_t) + string data
// <namespace>        ::= string length (uint8_t) + string data
// <entry_size>       ::= uint64_t ; = size of entry in uncompressed form
// <payload>          ::= depends on entry_type; potentially compressed
// <epilogue>         ::= <checksum_high> <checksum_low>
// <checksum_high>    ::= uint64_t ; XXH3-128 (high bits) of <header>+<payload>
// <checksum_low>     ::= uint64_t ; XXH3-128 (low bits) of <header>+<payload>

class Config;

namespace core {

const uint16_t k_ccache_magic = 0xccac;

class CacheEntry
{
public:
  static const uint8_t k_format_version;
  constexpr static uint8_t default_compression_level = 1;

  class Header
  {
  public:
    Header(const Config& config, CacheEntryType entry_type);
    explicit Header(nonstd::span<const uint8_t> data);
    explicit Header(const std::filesystem::path& path);

    std::string inspect() const;

    uint16_t magic;
    uint8_t entry_format_version;
    CacheEntryType entry_type;
    CompressionType compression_type;
    int8_t compression_level;
    bool self_contained;
    uint64_t creation_time;
    std::string ccache_version;
    std::string namespace_;
    uint64_t entry_size;

    size_t serialized_size() const;
    void serialize(util::Bytes& output) const;
    uint32_t uncompressed_payload_size() const;

  private:
    void parse(nonstd::span<const uint8_t> data);
  };

  explicit CacheEntry(nonstd::span<const uint8_t> data);

  void verify_checksum() const;
  const Header& header() const;

  // Return uncompressed payload.
  nonstd::span<const uint8_t> payload() const;

  static util::Bytes serialize(const Header& header,
                               Serializer& payload_serializer);
  static util::Bytes serialize(const Header& header,
                               nonstd::span<const uint8_t> payload);

private:
  Header m_header;
  nonstd::span<const uint8_t> m_payload; // Potentially compressed
  util::Bytes m_checksum;

  mutable util::Bytes m_uncompressed_payload;

  static util::Bytes
  do_serialize(const Header& header,
               size_t serialized_payload_size,
               std::function<void(util::Bytes& result, const Header& header)>
                 serialize_payload);
};

} // namespace core
