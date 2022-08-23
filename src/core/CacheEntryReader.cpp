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

#include "CacheEntryReader.hpp"

#include <core/exceptions.hpp>
#include <fmtmacros.hpp>

namespace {

core::CacheEntryType
cache_entry_type_from_int(const uint8_t entry_type)
{
  switch (entry_type) {
  case 0:
    return core::CacheEntryType::result;
    break;
  case 1:
    return core::CacheEntryType::manifest;
    break;
  default:
    throw core::Error(FMT("Unknown entry type: {}", entry_type));
  }
}

} // namespace

namespace core {

CacheEntryReader::CacheEntryReader(core::Reader& reader)
  : m_checksumming_reader(reader)
{
  const auto magic = m_checksumming_reader.read_int<uint16_t>();
  if (magic != core::k_ccache_magic) {
    throw core::Error(FMT("Bad magic value: 0x{:04x}", magic));
  }

  const auto entry_format_version = m_checksumming_reader.read_int<uint8_t>();
  if (entry_format_version != core::k_entry_format_version) {
    throw core::Error(
      FMT("Unknown entry format version: {}", entry_format_version));
  }

  const auto entry_type = m_checksumming_reader.read_int<uint8_t>();
  const auto compression_type = m_checksumming_reader.read_int<uint8_t>();
  const auto compression_level = m_checksumming_reader.read_int<int8_t>();
  const auto creation_time = m_checksumming_reader.read_int<uint64_t>();
  const auto ccache_version =
    m_checksumming_reader.read_str(m_checksumming_reader.read_int<uint8_t>());
  const auto tag =
    m_checksumming_reader.read_str(m_checksumming_reader.read_int<uint8_t>());
  const auto entry_size = m_checksumming_reader.read_int<uint64_t>();

  m_header = std::make_unique<CacheEntryHeader>(
    cache_entry_type_from_int(entry_type),
    compression::type_from_int(compression_type),
    compression_level,
    creation_time,
    ccache_version,
    tag,
    entry_size);

  m_decompressor = compression::Decompressor::create_from_type(
    m_header->compression_type, reader);
  m_checksumming_reader.set_reader(*m_decompressor);
}

size_t
CacheEntryReader::read(void* const data, const size_t count)
{
  return m_checksumming_reader.read(data, count);
}

void
CacheEntryReader::finalize()
{
  const util::XXH3_128::Digest actual = m_checksumming_reader.digest();
  util::XXH3_128::Digest expected;
  m_decompressor->read(expected.bytes(), expected.size());

  // actual == null_digest: Checksumming is not enabled now.
  // expected == null_digest: Checksumming was not enabled when the entry was
  // created.
  const util::XXH3_128::Digest null_digest;

  if (actual != expected && actual != null_digest && expected != null_digest) {
    throw core::Error(
      FMT("Incorrect checksum (actual {}, expected {})",
          Util::format_base16(actual.bytes(), actual.size()),
          Util::format_base16(expected.bytes(), expected.size())));
  }

  m_decompressor->finalize();
}

} // namespace core
