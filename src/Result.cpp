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

#include "Result.hpp"

#include "AtomicFile.hpp"
#include "Config.hpp"
#include "Context.hpp"
#include "Fd.hpp"
#include "File.hpp"
#include "Logging.hpp"
#include "Stat.hpp"
#include "Util.hpp"
#include "fmtmacros.hpp"

#include <ccache.hpp>
#include <core/CacheEntryReader.hpp>
#include <core/CacheEntryWriter.hpp>
#include <core/FileReader.hpp>
#include <core/FileWriter.hpp>
#include <core/Statistic.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <util/path.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>

// Result data format
// ==================
//
// Integers are big-endian.
//
// <payload>              ::= <format_ver> <n_entries> <entry>*
// <format_ver>           ::= uint8_t
// <n_entries>            ::= uint8_t
// <entry>                ::= <embedded_file_entry> | <raw_file_entry>
// <embedded_file_entry>  ::= <embedded_file_marker> <suffix_len> <suffix>
//                            <data_len> <data>
// <embedded_file_marker> ::= 0 (uint8_t)
// <embedded_file_type>   ::= uint8_t
// <data_len>             ::= uint64_t
// <data>                 ::= data_len bytes
// <raw_file_entry>       ::= <raw_file_marker> <suffix_len> <suffix> <file_len>
// <raw_file_marker>      ::= 1 (uint8_t)
// <file_len>             ::= uint64_t
// <epilogue>             ::= <checksum>
// <checksum>             ::= uint64_t ; XXH3 of content bytes

using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

namespace {

const uint8_t k_result_format_version = 0;

// File data stored inside the result file.
const uint8_t k_embedded_file_marker = 0;

// File stored as-is in the file system.
const uint8_t k_raw_file_marker = 1;

std::string
get_raw_file_path(string_view result_path, uint32_t entry_number)
{
  const auto prefix = result_path.substr(
    0, result_path.length() - Result::k_file_suffix.length());
  return FMT("{}{}W", prefix, entry_number);
}

bool
should_store_raw_file(const Config& config, Result::FileType type)
{
  if (!config.file_clone() && !config.hard_link()) {
    return false;
  }

  // Only store object files as raw files since there are several problems with
  // storing other file types:
  //
  // 1. The compiler unlinks object files before writing to them but it doesn't
  //    unlink .d files, so just it's possible to corrupt .d files just by
  //    running the compiler (see ccache issue 599).
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
  return type == Result::FileType::object;
}

} // namespace

namespace Result {

const std::string k_file_suffix = "R";
const uint8_t k_magic[4] = {'c', 'C', 'r', 'S'};
const uint8_t k_version = 1;
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
  }

  return k_unknown_file_type;
}

std::string
gcno_file_in_mangled_form(const Context& ctx)
{
  const auto& output_obj = ctx.args_info.output_obj;
  const std::string abs_output_obj =
    util::is_absolute_path(output_obj)
      ? output_obj
      : FMT("{}/{}", ctx.apparent_cwd, output_obj);
  std::string hashified_obj = abs_output_obj;
  std::replace(hashified_obj.begin(), hashified_obj.end(), '/', '#');
  return Util::change_extension(hashified_obj, ".gcno");
}

std::string
gcno_file_in_unmangled_form(const Context& ctx)
{
  return Util::change_extension(ctx.args_info.output_obj, ".gcno");
}

FileSizeAndCountDiff&
FileSizeAndCountDiff::operator+=(const FileSizeAndCountDiff& other)
{
  size_kibibyte += other.size_kibibyte;
  count += other.count;
  return *this;
}

Result::Reader::Reader(const std::string& result_path)
  : m_result_path(result_path)
{
}

optional<std::string>
Result::Reader::read(Consumer& consumer)
{
  LOG("Reading result {}", m_result_path);

  try {
    if (read_result(consumer)) {
      return nullopt;
    } else {
      return "No such result file";
    }
  } catch (const core::Error& e) {
    return e.what();
  }
}

bool
Reader::read_result(Consumer& consumer)
{
  FILE* file_stream;
  File file;
  if (m_result_path == "-") {
    file_stream = stdin;
  } else {
    file = File(m_result_path, "rb");
    if (!file) {
      // Cache miss.
      return false;
    }
    file_stream = file.get();
  }

  core::FileReader file_reader(file_stream);
  core::CacheEntryReader cache_entry_reader(file_reader);

  const auto result_format_version = cache_entry_reader.read_int<uint8_t>();
  if (result_format_version != k_result_format_version) {
    throw core::Error("Unknown result format version: {}",
                      result_format_version);
  }

  consumer.on_header(cache_entry_reader, result_format_version);

  const auto n_entries = cache_entry_reader.read_int<uint8_t>();

  uint32_t i;
  for (i = 0; i < n_entries; ++i) {
    read_entry(cache_entry_reader, i, consumer);
  }

  if (i != n_entries) {
    throw core::Error("Too few entries (read {}, expected {})", i, n_entries);
  }

  cache_entry_reader.finalize();
  return true;
}

void
Reader::read_entry(core::CacheEntryReader& cache_entry_reader,
                   uint32_t entry_number,
                   Reader::Consumer& consumer)
{
  const auto marker = cache_entry_reader.read_int<uint8_t>();

  switch (marker) {
  case k_embedded_file_marker:
  case k_raw_file_marker:
    break;

  default:
    throw core::Error("Unknown entry type: {}", marker);
  }

  const auto type = cache_entry_reader.read_int<UnderlyingFileTypeInt>();
  const auto file_type = FileType(type);
  const auto file_len = cache_entry_reader.read_int<uint64_t>();

  if (marker == k_embedded_file_marker) {
    consumer.on_entry_start(entry_number, file_type, file_len, nullopt);

    uint8_t buf[CCACHE_READ_BUFFER_SIZE];
    size_t remain = file_len;
    while (remain > 0) {
      size_t n = std::min(remain, sizeof(buf));
      cache_entry_reader.read(buf, n);
      consumer.on_entry_data(buf, n);
      remain -= n;
    }
  } else {
    ASSERT(marker == k_raw_file_marker);

    auto raw_path = get_raw_file_path(m_result_path, entry_number);
    auto st = Stat::stat(raw_path, Stat::OnError::throw_error);
    if (st.size() != file_len) {
      throw core::Error(
        "Bad file size of {} (actual {} bytes, expected {} bytes)",
        raw_path,
        st.size(),
        file_len);
    }

    consumer.on_entry_start(entry_number, file_type, file_len, raw_path);
  }

  consumer.on_entry_end();
}

Writer::Writer(Context& ctx, const std::string& result_path)
  : m_ctx(ctx),
    m_result_path(result_path)
{
}

void
Writer::write(FileType file_type, const std::string& file_path)
{
  m_entries_to_write.emplace_back(file_type, file_path);
}

nonstd::expected<FileSizeAndCountDiff, std::string>
Writer::finalize()
{
  try {
    return do_finalize();
  } catch (const core::Error& e) {
    return nonstd::make_unexpected(e.what());
  }
}

FileSizeAndCountDiff
Writer::do_finalize()
{
  FileSizeAndCountDiff file_size_and_count_diff{0, 0};
  uint64_t payload_size = 0;
  payload_size += 1; // n_entries
  for (const auto& pair : m_entries_to_write) {
    const auto& path = pair.second;
    auto st = Stat::stat(path, Stat::OnError::throw_error);

    payload_size += 1;         // embedded_file_marker
    payload_size += 1;         // embedded_file_type
    payload_size += 8;         // data_len
    payload_size += st.size(); // data
  }

  AtomicFile atomic_result_file(m_result_path, AtomicFile::Mode::binary);
  core::CacheEntryHeader header(core::CacheEntryType::result,
                                compression::type_from_config(m_ctx.config),
                                compression::level_from_config(m_ctx.config),
                                time(nullptr),
                                CCACHE_VERSION,
                                m_ctx.config.namespace_());
  header.set_entry_size_from_payload_size(payload_size);

  core::FileWriter file_writer(atomic_result_file.stream());
  core::CacheEntryWriter writer(file_writer, header);

  writer.write_int(k_result_format_version);
  writer.write_int<uint8_t>(m_entries_to_write.size());

  uint32_t entry_number = 0;
  for (const auto& pair : m_entries_to_write) {
    const auto file_type = pair.first;
    const auto& path = pair.second;
    LOG("Storing result file {}", path);

    const bool store_raw = should_store_raw_file(m_ctx.config, file_type);
    uint64_t file_size = Stat::stat(path, Stat::OnError::throw_error).size();

    LOG("Storing {} file #{} {} ({} bytes) from {}",
        store_raw ? "raw" : "embedded",
        entry_number,
        file_type_to_string(file_type),
        file_size,
        path);

    writer.write_int<uint8_t>(store_raw ? k_raw_file_marker
                                        : k_embedded_file_marker);
    writer.write_int(UnderlyingFileTypeInt(file_type));
    writer.write_int(file_size);

    if (store_raw) {
      file_size_and_count_diff += write_raw_file_entry(path, entry_number);
    } else {
      write_embedded_file_entry(writer, path, file_size);
    }

    ++entry_number;
  }

  writer.finalize();
  atomic_result_file.commit();

  return file_size_and_count_diff;
}

void
Result::Writer::write_embedded_file_entry(core::CacheEntryWriter& writer,
                                          const std::string& path,
                                          uint64_t file_size)
{
  Fd file(open(path.c_str(), O_RDONLY | O_BINARY));
  if (!file) {
    throw core::Error("Failed to open {} for reading", path);
  }

  uint64_t remain = file_size;
  while (remain > 0) {
    uint8_t buf[CCACHE_READ_BUFFER_SIZE];
    size_t n = std::min(remain, static_cast<uint64_t>(sizeof(buf)));
    auto bytes_read = read(*file, buf, n);
    if (bytes_read == -1) {
      if (errno == EINTR) {
        continue;
      }
      throw core::Error("Error reading from {}: {}", path, strerror(errno));
    }
    if (bytes_read == 0) {
      throw core::Error("Error reading from {}: end of file", path);
    }
    writer.write(buf, bytes_read);
    remain -= bytes_read;
  }
}

FileSizeAndCountDiff
Result::Writer::write_raw_file_entry(const std::string& path,
                                     uint32_t entry_number)
{
  const auto raw_file = get_raw_file_path(m_result_path, entry_number);
  const auto old_stat = Stat::stat(raw_file);
  try {
    Util::clone_hard_link_or_copy_file(m_ctx, path, raw_file, true);
  } catch (core::Error& e) {
    throw core::Error(
      "Failed to store {} as raw file {}: {}", path, raw_file, e.what());
  }
  const auto new_stat = Stat::stat(raw_file);
  return {
    Util::size_change_kibibyte(old_stat, new_stat),
    (new_stat ? 1 : 0) - (old_stat ? 1 : 0),
  };
}

} // namespace Result
