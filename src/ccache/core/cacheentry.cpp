// Copyright (C) 2022-2025 Joel Rosdahl and other contributors
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

#include "cacheentry.hpp"

#include <ccache/ccache.hpp>
#include <ccache/core/cacheentrydatareader.hpp>
#include <ccache/core/cacheentrydatawriter.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/core/result.hpp>
#include <ccache/core/types.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/xxh3_128.hpp>
#include <ccache/util/zstd.hpp>

#include <cstring>

namespace fs = util::filesystem;

namespace {

const size_t k_static_header_fields_size =
  sizeof(core::CacheEntry::Header::magic)
  + sizeof(core::CacheEntry::Header::entry_format_version)
  + sizeof(core::CacheEntry::Header::entry_type)
  + sizeof(core::CacheEntry::Header::compression_type)
  + sizeof(core::CacheEntry::Header::compression_level)
  + sizeof(core::CacheEntry::Header::self_contained)
  + sizeof(core::CacheEntry::Header::creation_time)
  + sizeof(core::CacheEntry::Header::entry_size)
  // ccache_version length field:
  + 1
  // namespace_ length field:
  + 1;

const size_t k_epilogue_fields_size = sizeof(uint64_t) + sizeof(uint64_t);

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

// Version 0:
//   - First version.
// Version 1:
//   - Added self_contained field.
//   - The checksum is now for the (potentially) compressed payload instead of
//     the uncompressed payload, and the checksum is now always stored
//     uncompressed.
const uint8_t CacheEntry::k_format_version = 1;

CacheEntry::Header::Header(const Config& config,
                           core::CacheEntryType entry_type_)
  : magic(k_ccache_magic),
    entry_format_version(k_format_version),
    entry_type(entry_type_),
    compression_type(compression_type_from_config(config)),
    compression_level(compression_level_from_config(config)),
    self_contained(entry_type != CacheEntryType::result
                   || !core::result::Serializer::use_raw_files(config)),
    creation_time(util::sec(util::now())),
    ccache_version(CCACHE_VERSION),
    namespace_(config.namespace_()),
    entry_size(0)
{
  if (compression_type == CompressionType::none) {
    LOG_RAW("Using no compression");
  } else if (compression_level == 0) {
    compression_level = default_compression_level;
    LOG("Using Zstandard with default compression level {}", compression_level);
  } else {
    LOG("Using Zstandard with compression level {}", compression_level);
  }
}

CacheEntry::Header::Header(nonstd::span<const uint8_t> data)
{
  parse(data);
}

CacheEntry::Header::Header(const fs::path& path)
{
  parse(util::value_or_throw<core::Error>(
    util::read_file_part<util::Bytes>(path, 0, 1000)));
}

std::string
CacheEntry::Header::inspect() const
{
  std::string result;
  result += FMT("Magic: {:04x}\n", magic);
  result += FMT("Entry format version: {}\n", entry_format_version);
  result += FMT("Entry type: {} ({})\n",
                static_cast<uint8_t>(entry_type),
                to_string(entry_type));
  result += FMT("Compression type: {}\n", to_string(compression_type));
  result += FMT("Compression level: {}\n", compression_level);
  result += FMT("Self-contained: {}\n", self_contained ? "yes" : "no");
  result += FMT("Creation time: {}\n", creation_time);
  result += FMT("Ccache version: {}\n", ccache_version);
  result += FMT("Namespace: {}\n", namespace_);
  result += FMT("Entry size: {}\n", entry_size);
  return result;
}

void
CacheEntry::Header::parse(nonstd::span<const uint8_t> data)
{
  CacheEntryDataReader reader(data);
  reader.read_int(magic);
  if (magic != core::k_ccache_magic) {
    throw core::Error(FMT("Bad magic value: 0x{:04x}", magic));
  }

  reader.read_int(entry_format_version);
  if (entry_format_version != k_format_version) {
    throw core::Error(
      FMT("Unknown entry format version: {}", entry_format_version));
  }

  entry_type = cache_entry_type_from_int(reader.read_int<uint8_t>());
  compression_type = compression_type_from_int(reader.read_int<uint8_t>());
  reader.read_int(compression_level);
  self_contained = bool(reader.read_int<uint8_t>());
  reader.read_int(creation_time);
  ccache_version = reader.read_str(reader.read_int<uint8_t>());
  namespace_ = reader.read_str(reader.read_int<uint8_t>());
  reader.read_int(entry_size);
}

size_t
CacheEntry::Header::serialized_size() const
{
  return k_static_header_fields_size + ccache_version.length()
         + namespace_.length();
}

void
CacheEntry::Header::serialize(util::Bytes& output) const
{
  core::CacheEntryDataWriter writer(output);
  writer.write_int(magic);
  writer.write_int(entry_format_version);
  writer.write_int(static_cast<uint8_t>(entry_type));
  writer.write_int(static_cast<uint8_t>(compression_type));
  writer.write_int(compression_level);
  writer.write_int<uint8_t>(self_contained);
  writer.write_int(creation_time);
  writer.write_int(static_cast<uint8_t>(ccache_version.length()));
  writer.write_str(ccache_version);
  writer.write_int(static_cast<uint8_t>(namespace_.length()));
  writer.write_str(namespace_);
  writer.write_int(entry_size);
}

uint32_t
CacheEntry::Header::uncompressed_payload_size() const
{
  return static_cast<uint32_t>(entry_size - serialized_size()
                               - k_epilogue_fields_size);
}

CacheEntry::CacheEntry(nonstd::span<const uint8_t> data)
  : m_header(data)
{
  const size_t non_payload_size =
    m_header.serialized_size() + k_epilogue_fields_size;
  if (data.size() <= non_payload_size) {
    throw core::Error("CacheEntry data underflow");
  }
  m_payload =
    data.subspan(m_header.serialized_size(), data.size() - non_payload_size);
  m_checksum = data.last(k_epilogue_fields_size);

  switch (m_header.compression_type) {
  case CompressionType::none:
    break;

  case CompressionType::zstd:
    m_uncompressed_payload.reserve(m_header.uncompressed_payload_size());
    util::throw_on_error<core::Error>(
      util::zstd_decompress(
        m_payload, m_uncompressed_payload, m_uncompressed_payload.capacity()),
      "Cache entry payload decompression error: ");
    break;
  }
}

void
CacheEntry::verify_checksum() const
{
  util::Bytes header_data;
  m_header.serialize(header_data);

  util::XXH3_128 checksum;
  checksum.update(header_data);
  checksum.update(m_payload);
  const auto actual = checksum.digest();

  if (actual != m_checksum) {
    throw core::Error(FMT("Incorrect checksum (actual {}, expected {})",
                          util::format_base16(actual),
                          util::format_base16(m_checksum)));
  }
}

const CacheEntry::Header&
CacheEntry::header() const
{
  return m_header;
}

nonstd::span<const uint8_t>
CacheEntry::payload() const
{
  return m_header.compression_type == CompressionType::none
           ? m_payload
           : nonstd::span<const uint8_t>(m_uncompressed_payload);
}

util::Bytes
CacheEntry::serialize(const CacheEntry::Header& header,
                      Serializer& payload_serializer)
{
  return do_serialize(
    header,
    payload_serializer.serialized_size(),
    [&payload_serializer](util::Bytes& result, const CacheEntry::Header& hdr) {
      switch (hdr.compression_type) {
      case CompressionType::none:
        payload_serializer.serialize(result);
        break;

      case CompressionType::zstd:
        util::Bytes payload;
        payload_serializer.serialize(payload);
        util::throw_on_error<core::Error>(
          util::zstd_compress(payload, result, hdr.compression_level),
          "Cache entry payload compression error: ");
        break;
      }
    });
}

util::Bytes
CacheEntry::serialize(const CacheEntry::Header& header,
                      nonstd::span<const uint8_t> payload)
{
  return do_serialize(
    header,
    payload.size(),
    [&payload](util::Bytes& result, const CacheEntry::Header& hdr) {
      switch (hdr.compression_type) {
      case CompressionType::none:
        result.insert(result.end(), payload);
        break;

      case CompressionType::zstd:
        util::throw_on_error<core::Error>(
          util::zstd_compress(payload, result, hdr.compression_level),
          "Cache entry payload compression error: ");
        break;
      }
    });
}

util::Bytes
CacheEntry::do_serialize(
  const CacheEntry::Header& header,
  size_t serialized_payload_size,
  std::function<void(util::Bytes& result, const Header& hdr)> serialize_payload)
{
  CacheEntry::Header hdr(header);
  const size_t non_payload_size =
    hdr.serialized_size() + k_epilogue_fields_size;
  hdr.entry_size = non_payload_size + serialized_payload_size;

  if (hdr.compression_type == CompressionType::zstd) {
    const auto [level, explanation] =
      util::zstd_supported_compression_level(hdr.compression_level);
    if (!explanation.empty()) {
      LOG("Using ZSTD compression level {} ({}) instead of {}",
          level,
          explanation,
          hdr.compression_level);
    }
    hdr.compression_level = level;
  }

  const size_t max_serialized_size =
    hdr.compression_type == CompressionType::zstd
      ? (non_payload_size + util::zstd_compress_bound(serialized_payload_size))
      : hdr.entry_size;
  util::Bytes result;
  result.reserve(max_serialized_size);

  hdr.serialize(result);
  serialize_payload(result, hdr);

  util::XXH3_128 checksum;
  checksum.update(result);
  const auto digest = checksum.digest();
  result.insert(result.end(), digest.begin(), digest.end());

  return result;
}

} // namespace core
