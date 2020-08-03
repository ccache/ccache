// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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
#include "CacheEntryReader.hpp"
#include "CacheEntryWriter.hpp"
#include "Config.hpp"
#include "Context.hpp"
#include "Fd.hpp"
#include "File.hpp"
#include "Logging.hpp"
#include "Stat.hpp"
#include "Util.hpp"
#include "exceptions.hpp"
#include "stats.hpp"

// Result data format
// ==================
//
// Integers are big-endian.
//
// <result>               ::= <header> <body> <epilogue>
// <header>               ::= <magic> <version> <compr_type> <compr_level>
//                            <content_len>
// <magic>                ::= 4 bytes ("cCrS")
// <version>              ::= uint8_t
// <compr_type>           ::= <compr_none> | <compr_zstd>
// <compr_none>           ::= 0 (uint8_t)
// <compr_zstd>           ::= 1 (uint8_t)
// <compr_level>          ::= int8_t
// <content_len>          ::= uint64_t ; size of file if stored uncompressed
// <body>                 ::= <n_entries> <entry>* ; potentially compressed
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
// <checksum>             ::= uint64_t ; XXH64 of content bytes
//
// Sketch of concrete layout:
//
// <magic>                4 bytes
// <version>              1 byte
// <compr_type>           1 byte
// <compr_level>          1 byte
// <content_len>          8 bytes
// --- [potentially compressed from here] -------------------------------------
// <n_entries>            1 byte
// <embedded_file_marker> 1 byte
// <embedded_file_type>   1 byte
// <data_len>             8 bytes
// <data>                 data_len bytes
// ...
// <ref_marker>           1 byte
// <key_len>              1 byte
// <key>                  key_len bytes
// ...
// checksum               8 bytes
//
//
// Version history
// ===============
//
// 1: Introduced in ccache 4.0.

using nonstd::nullopt;
using nonstd::optional;

namespace {

// File data stored inside the result file.
const uint8_t k_embedded_file_marker = 0;

// File stored as-is in the file system.
const uint8_t k_raw_file_marker = 1;

std::string
get_raw_file_path(const std::string& result_path_in_cache,
                  uint32_t entry_number)
{
  return fmt::format("{:{}}_{}.raw",
                     result_path_in_cache.c_str(),
                     result_path_in_cache.length() - 7, // .result
                     entry_number);
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

  case FileType::coverage:
    return ".cov";

  case FileType::stackusage:
    return ".su";

  case FileType::diagnostic:
    return ".dia";

  case FileType::dwarf_object:
    return ".dwo";
  }

  return k_unknown_file_type;
}

Result::Reader::Reader(const std::string& result_path)
  : m_result_path(result_path)
{
}

optional<std::string>
Result::Reader::read(Consumer& consumer)
{
  cc_log("Reading result %s", m_result_path.c_str());

  try {
    if (read_result(consumer)) {
      return nullopt;
    } else {
      return "No such result file";
    }
  } catch (const Error& e) {
    return e.what();
  }
}

bool
Reader::read_result(Consumer& consumer)
{
  File file(m_result_path, "rb");
  if (!file) {
    // Cache miss.
    return false;
  }

  CacheEntryReader cache_entry_reader(file.get(), k_magic, k_version);

  consumer.on_header(cache_entry_reader);

  uint8_t n_entries;
  cache_entry_reader.read(n_entries);

  uint32_t i;
  for (i = 0; i < n_entries; ++i) {
    read_entry(cache_entry_reader, i, consumer);
  }

  if (i != n_entries) {
    throw Error(
      fmt::format("Too few entries (read {}, expected {})", i, n_entries));
  }

  cache_entry_reader.finalize();
  return true;
}

void
Reader::read_entry(CacheEntryReader& cache_entry_reader,
                   uint32_t entry_number,
                   Reader::Consumer& consumer)
{
  uint8_t marker;
  cache_entry_reader.read(marker);

  switch (marker) {
  case k_embedded_file_marker:
  case k_raw_file_marker:
    break;

  default:
    throw Error(fmt::format("Unknown entry type: {}", marker));
  }

  UnderlyingFileTypeInt type;
  cache_entry_reader.read(type);
  FileType file_type = FileType(type);

  uint64_t file_len;
  cache_entry_reader.read(file_len);

  if (marker == k_embedded_file_marker) {
    consumer.on_entry_start(entry_number, file_type, file_len, nullopt);

    uint8_t buf[READ_BUFFER_SIZE];
    size_t remain = file_len;
    while (remain > 0) {
      size_t n = std::min(remain, sizeof(buf));
      cache_entry_reader.read(buf, n);
      consumer.on_entry_data(buf, n);
      remain -= n;
    }
  } else {
    assert(marker == k_raw_file_marker);

    auto raw_path = get_raw_file_path(m_result_path, entry_number);
    auto st = Stat::stat(raw_path, Stat::OnError::throw_error);
    if (st.size() != file_len) {
      throw Error(
        fmt::format("Bad file size of {} (actual {} bytes, expected {} bytes)",
                    raw_path,
                    st.size(),
                    file_len));
    }

    consumer.on_entry_start(entry_number, file_type, file_len, raw_path);
  }

  consumer.on_entry_end();
}

Writer::Writer(Context& ctx, const std::string& result_path)
  : m_ctx(ctx), m_result_path(result_path)
{
}

Writer::~Writer()
{
  if (!m_finalized) {
    finalize();
  }
}

void
Writer::write(FileType file_type, const std::string& file_path)
{
  m_entries_to_write.emplace_back(file_type, file_path);
}

optional<std::string>
Writer::finalize()
{
  m_finalized = true;
  try {
    do_finalize();
    return nullopt;
  } catch (const Error& e) {
    return e.what();
  }
}

void
Writer::do_finalize()
{
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
  CacheEntryWriter writer(atomic_result_file.stream(),
                          k_magic,
                          k_version,
                          Compression::type_from_config(m_ctx.config),
                          Compression::level_from_config(m_ctx.config),
                          payload_size);

  writer.write<uint8_t>(m_entries_to_write.size());

  uint32_t entry_number = 0;
  for (const auto& pair : m_entries_to_write) {
    const auto file_type = pair.first;
    const auto& path = pair.second;
    cc_log("Storing result %s", path.c_str());

    const bool store_raw = should_store_raw_file(m_ctx.config, file_type);
    uint64_t file_size = Stat::stat(path, Stat::OnError::throw_error).size();

    cc_log("Storing %s file #%u %s (%llu bytes) from %s",
           store_raw ? "raw" : "embedded",
           entry_number,
           file_type_to_string(file_type),
           (unsigned long long)file_size,
           path.c_str());

    writer.write<uint8_t>(store_raw ? k_raw_file_marker
                                    : k_embedded_file_marker);
    writer.write(UnderlyingFileTypeInt(file_type));
    writer.write(file_size);

    if (store_raw) {
      write_raw_file_entry(path, entry_number);
    } else {
      write_embedded_file_entry(writer, path, file_size);
    }

    ++entry_number;
  }

  writer.finalize();
  atomic_result_file.commit();
}

void
Result::Writer::write_embedded_file_entry(CacheEntryWriter& writer,
                                          const std::string& path,
                                          uint64_t file_size)
{
  Fd file(open(path.c_str(), O_RDONLY | O_BINARY));
  if (!file) {
    throw Error(fmt::format("Failed to open {} for reading", path));
  }

  uint64_t remain = file_size;
  while (remain > 0) {
    uint8_t buf[READ_BUFFER_SIZE];
    size_t n = std::min(remain, static_cast<uint64_t>(sizeof(buf)));
    ssize_t bytes_read = read(*file, buf, n);
    if (bytes_read == -1) {
      if (errno == EINTR) {
        continue;
      }
      throw Error(
        fmt::format("Error reading from {}: {}", path, strerror(errno)));
    }
    if (bytes_read == 0) {
      throw Error(fmt::format("Error reading from {}: end of file", path));
    }
    writer.write(buf, n);
    remain -= n;
  }
}

void
Result::Writer::write_raw_file_entry(const std::string& path,
                                     uint32_t entry_number)
{
  auto raw_file = get_raw_file_path(m_result_path, entry_number);
  auto old_stat = Stat::stat(raw_file);
  try {
    Util::clone_hard_link_or_copy_file(m_ctx, path, raw_file, true);
  } catch (Error& e) {
    throw Error(fmt::format(
      "Failed to store {} as raw file {}: {}", path, raw_file, e.what()));
  }
  auto new_stat = Stat::stat(raw_file);
  stats_update_size(m_ctx.counter_updates,
                    new_stat.size_on_disk() - old_stat.size_on_disk(),
                    (new_stat ? 1 : 0) - (old_stat ? 1 : 0));
}

} // namespace Result
