// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include "Result.hpp"

#include "Config.hpp"
#include "Context.hpp"

#include <ccache.hpp>
#include <core/CacheEntryDataReader.hpp>
#include <core/CacheEntryDataWriter.hpp>
#include <core/Statistic.hpp>
#include <core/exceptions.hpp>
#include <util/Bytes.hpp>
#include <util/DirEntry.hpp>
#include <util/FileStream.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/logging.hpp>
#include <util/path.hpp>
#include <util/string.hpp>
#include <util/wincompat.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>

namespace fs = util::filesystem;

// Result data format
// ==================
//
// Integers are big-endian.
//
// <payload>              ::= <format_ver> <n_files> <file_entry>*
// <format_ver>           ::= uint8_t
// <n_files>              ::= uint8_t
// <file_entry>           ::= <embedded_file_entry> | <raw_file_entry>
// <embedded_file_entry>  ::= <embedded_file_marker> <file_type> <file_size>
//                            <file_data>
// <embedded_file_marker> ::= 0 (uint8_t)
// <file_type>            ::= uint8_t (see Result::FileType)
// <file_size>            ::= uint64_t
// <file_data>            ::= file_size bytes
// <raw_file_entry>       ::= <raw_file_marker> <file_type> <file_size>
// <raw_file_marker>      ::= 1 (uint8_t)
// <file_size>            ::= uint64_t

using util::DirEntry;

namespace {

// File data stored inside the result file.
const uint8_t k_embedded_file_marker = 0;

// File stored as-is in the file system.
const uint8_t k_raw_file_marker = 1;

const uint8_t k_max_raw_file_entries = 10;

bool
should_store_raw_file(const Config& config, core::Result::FileType type)
{
  if (!core::Result::Serializer::use_raw_files(config)) {
    return false;
  }

  // Only store object files as raw files since there are several problems with
  // storing other file types:
  //
  // 1. The compiler unlinks object files before writing to them but it doesn't
  //    unlink .d files, so it's possible to corrupt .d files just by running
  //    the compiler (see ccache issue 599).
  // 2. .d files cause trouble for automake if hard-linked (see ccache issue
  //    378).
  // 3. It's unknown how the compiler treats other file types, so better safe
  //    than sorry.
  //
  // It would be possible to store all files in raw form for the file_clone case
  // and only hard link object files. However, most likely it's only object
  // files that become large enough that it's of interest to clone or hard link
  // them, so we keep things simple for now. This will also save i-nodes in the
  // cache.
  return type == core::Result::FileType::object;
}

} // namespace

namespace core::Result {

const uint8_t k_format_version = 0;

const char* const k_unknown_file_type = "<unknown type>";

const char*
file_type_to_string(FileType type)
{
  switch (type) {
  case FileType::object:
    return ".o";

  case FileType::dependency:
    return ".d";

  case FileType::stderr_output:
    return "<stderr>";

  case FileType::coverage_unmangled:
    return ".gcno-unmangled";

  case FileType::stackusage:
    return ".su";

  case FileType::diagnostic:
    return ".dia";

  case FileType::dwarf_object:
    return ".dwo";

  case FileType::coverage_mangled:
    return ".gcno-mangled";

  case FileType::stdout_output:
    return "<stdout>";

  case FileType::assembler_listing:
    return ".al";
  }

  return k_unknown_file_type;
}

std::string
gcno_file_in_mangled_form(const Context& ctx)
{
  const auto& output_obj = ctx.args_info.output_obj;
  const std::string abs_output_obj =
    fs::path(output_obj).is_absolute()
      ? output_obj
      : FMT("{}/{}", ctx.apparent_cwd, output_obj);
  std::string hashified_obj = abs_output_obj;
  std::replace(hashified_obj.begin(), hashified_obj.end(), '/', '#');
  return fs::path(hashified_obj).replace_extension(".gcno").string();
}

std::string
gcno_file_in_unmangled_form(const Context& ctx)
{
  return fs::path(ctx.args_info.output_obj).replace_extension(".gcno").string();
}

Deserializer::Deserializer(nonstd::span<const uint8_t> data) : m_data(data)
{
}

void
Deserializer::visit(Deserializer::Visitor& visitor) const
{
  Header header;

  CacheEntryDataReader reader(m_data);
  header.format_version = reader.read_int<uint8_t>();
  if (header.format_version != k_format_version) {
    visitor.on_header(header);
    throw Error(FMT("Unknown result format version: {} != {}",
                    header.format_version,
                    k_format_version));
  }

  header.n_files = reader.read_int<uint8_t>();
  if (header.n_files >= k_max_raw_file_entries) {
    visitor.on_header(header);
    throw Error(FMT("Too many raw file entries: {} > {}",
                    header.n_files,
                    k_max_raw_file_entries));
  }

  visitor.on_header(header);

  uint8_t file_number;
  for (file_number = 0; file_number < header.n_files; ++file_number) {
    const auto marker = reader.read_int<uint8_t>();
    switch (marker) {
    case k_embedded_file_marker:
    case k_raw_file_marker:
      break;

    default:
      throw Error(FMT("Unknown entry type: {}", marker));
    }

    const auto type = reader.read_int<UnderlyingFileTypeInt>();
    const auto file_type = FileType(type);
    const auto file_size = reader.read_int<uint64_t>();

    if (marker == k_embedded_file_marker) {
      visitor.on_embedded_file(
        file_number, file_type, reader.read_bytes(file_size));
    } else {
      ASSERT(marker == k_raw_file_marker);
      visitor.on_raw_file(file_number, file_type, file_size);
    }
  }

  if (file_number != header.n_files) {
    throw Error(FMT(
      "Too few entries (read {}, expected {})", file_number, header.n_files));
  }
}

Serializer::Serializer(const Config& config)
  : m_config(config),
    m_serialized_size(1 + 1) // format_ver + n_files
{
}

void
Serializer::add_data(const FileType file_type, nonstd::span<const uint8_t> data)
{
  m_serialized_size += 1 + 1 + 8; // marker + file_type + file_size
  m_serialized_size += data.size();
  m_file_entries.push_back(FileEntry{file_type, data});
}

bool
Serializer::add_file(const FileType file_type, const std::string& path)
{
  m_serialized_size += 1 + 1 + 8; // marker + file_type + file_size
  if (!should_store_raw_file(m_config, file_type)) {
    DirEntry entry(path);
    if (!entry.is_regular_file()) {
      return false;
    }
    m_serialized_size += entry.size();
  }
  m_file_entries.push_back(FileEntry{file_type, path});
  return true;
}

uint32_t
Serializer::serialized_size() const
{
  // In order to support 32-bit ccache builds, restrict size to uint32_t for
  // now. This restriction can be lifted when we drop 32-bit support.
  const auto max = std::numeric_limits<uint32_t>::max();
  if (m_serialized_size > max) {
    throw Error(
      FMT("Serialized result too large ({} > {})", m_serialized_size, max));
  }
  return static_cast<uint32_t>(m_serialized_size);
}

void
Serializer::serialize(util::Bytes& output)
{
  CacheEntryDataWriter writer(output);

  writer.write_int(k_format_version);
  writer.write_int(static_cast<uint8_t>(m_file_entries.size()));

  uint8_t file_number = 0;
  for (const auto& entry : m_file_entries) {
    const bool is_file_entry = std::holds_alternative<std::string>(entry.data);
    const bool store_raw =
      is_file_entry && should_store_raw_file(m_config, entry.file_type);
    const uint64_t file_size =
      is_file_entry
        ? DirEntry(std::get<std::string>(entry.data), DirEntry::LogOnError::yes)
            .size()
        : std::get<nonstd::span<const uint8_t>>(entry.data).size();

    LOG("Storing {} entry #{} {} ({} bytes){}",
        store_raw ? "raw" : "embedded",
        file_number,
        file_type_to_string(entry.file_type),
        file_size,
        is_file_entry ? FMT(" from {}", std::get<std::string>(entry.data))
                      : "");

    writer.write_int<uint8_t>(store_raw ? k_raw_file_marker
                                        : k_embedded_file_marker);
    writer.write_int(UnderlyingFileTypeInt(entry.file_type));
    writer.write_int(file_size);

    if (store_raw) {
      m_raw_files.push_back(
        RawFile{file_number, std::get<std::string>(entry.data)});
    } else if (is_file_entry) {
      const auto& path = std::get<std::string>(entry.data);
      const auto data = util::value_or_throw<Error>(
        util::read_file<util::Bytes>(path), FMT("Failed to read {}: ", path));
      writer.write_bytes(data);
    } else {
      writer.write_bytes(std::get<nonstd::span<const uint8_t>>(entry.data));
    }

    ++file_number;
  }
}

bool
Serializer::use_raw_files(const Config& config)
{
  return config.file_clone() || config.hard_link();
}

const std::vector<Serializer::RawFile>&
Serializer::get_raw_files() const
{
  return m_raw_files;
}

} // namespace core::Result
