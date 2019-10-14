// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "result.hpp"

#include "AtomicFile.hpp"
#include "CacheEntryReader.hpp"
#include "CacheEntryWriter.hpp"
#include "Config.hpp"
#include "Error.hpp"
#include "File.hpp"
#include "Util.hpp"
#include "ccache.hpp"

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
// <suffix_len>           ::= uint8_t
// <suffix>               ::= suffix_len bytes
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
// <suffix_len>           1 byte
// <suffix>               suffix_len bytes
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

extern char* stats_file;

const uint8_t k_result_magic[4] = {'c', 'C', 'r', 'S'};
const uint8_t k_result_version = 1;
const std::string k_result_stderr_name = "<stderr>";

// File data stored inside the result file.
const uint8_t k_embedded_file_marker = 0;

// File stored as-is in the file system.
const uint8_t k_raw_file_marker = 1;

using ReadEntryFunction = void (*)(CacheEntryReader& reader,
                                   const std::string& result_path_in_cache,
                                   uint32_t entry_number,
                                   const ResultFileMap* result_file_map,
                                   FILE* dump_stream);

using WriteEntryFunction =
  void (*)(CacheEntryWriter& writer,
           const std::string& result_path_in_cache,
           uint32_t entry_number,
           const ResultFileMap::value_type& suffix_and_path);

static void
read_embedded_file_entry(CacheEntryReader& reader,
                         const std::string& /*result_path_in_cache*/,
                         uint32_t entry_number,
                         const ResultFileMap* result_file_map,
                         FILE* dump_stream)
{
  uint8_t suffix_len;
  reader.read(suffix_len);

  char suffix[256];
  reader.read(suffix, suffix_len);

  uint64_t file_len;
  reader.read(file_len);

  bool content_read = false;
  std::string suffix_str(suffix, suffix_len);
  if (dump_stream) {
    fmt::print(dump_stream,
               "Embedded file #{}: {} ({} bytes)\n",
               entry_number,
               suffix_str,
               file_len);
  } else {
    cc_log("Retrieving embedded file #%u %s (%llu bytes)",
           entry_number,
           suffix_str.c_str(),
           (unsigned long long)file_len);

    const auto it = result_file_map->find(suffix_str);
    if (it != result_file_map->end()) {
      content_read = true;

      const auto& path = it->second;
      cc_log("Copying to %s", path.c_str());

      File subfile(path, "wb");
      if (!subfile) {
        throw Error(fmt::format(
          "Failed to open {} for writing: {}", path, strerror(errno)));
      }
      uint8_t buf[READ_BUFFER_SIZE];
      size_t remain = file_len;
      while (remain > 0) {
        size_t n = std::min(remain, sizeof(buf));
        reader.read(buf, n);
        if (fwrite(buf, n, 1, subfile.get()) != 1) {
          throw Error(fmt::format("Failed to write to {}", path));
        }
        remain -= n;
      }
    }
  }

  if (!content_read) {
    // Discard the file data.
    uint8_t buf[READ_BUFFER_SIZE];
    size_t remain = file_len;
    while (remain > 0) {
      size_t n = std::min(remain, sizeof(buf));
      reader.read(buf, n);
      remain -= n;
    }
  }
}

static std::string
get_raw_file_path(const std::string& result_path_in_cache,
                  uint32_t entry_number)
{
  return fmt::format("{:{}}_{}.raw",
                     result_path_in_cache.c_str(),
                     result_path_in_cache.length() - 7, // .result
                     entry_number);
}

static bool
copy_raw_file(const std::string& source, const std::string& dest, bool to_cache)
{
  if (g_config.file_clone()) {
    cc_log("Cloning %s to %s", source.c_str(), dest.c_str());
    if (clone_file(source.c_str(), dest.c_str(), to_cache)) {
      return true;
    }
    cc_log("Failed to clone: %s", strerror(errno));
  }
  if (g_config.hard_link()) {
    x_try_unlink(dest.c_str());
    cc_log("Hard linking %s to %s", source.c_str(), dest.c_str());
    int ret = link(source.c_str(), dest.c_str());
    if (ret == 0) {
      return true;
    }
    cc_log("Failed to hard link: %s", strerror(errno));
  }

  cc_log("Copying %s to %s", source.c_str(), dest.c_str());
  return copy_file(source.c_str(), dest.c_str(), to_cache);
}

static void
read_raw_file_entry(CacheEntryReader& reader,
                    const std::string& result_path_in_cache,
                    uint32_t entry_number,
                    const ResultFileMap* result_file_map,
                    std::FILE* dump_stream)
{
  uint8_t suffix_len;
  reader.read(suffix_len);

  char suffix[256];
  reader.read(suffix, suffix_len);

  uint64_t file_len;
  reader.read(file_len);

  std::string suffix_str(suffix, suffix_len);
  if (dump_stream) {
    fmt::print(dump_stream,
               "Raw file #{}: {} ({} bytes)\n",
               entry_number,
               suffix_str,
               file_len);
  } else {
    cc_log("Retrieving raw file #%u %s (%llu bytes)",
           entry_number,
           suffix_str.c_str(),
           (unsigned long long)file_len);

    auto raw_path = get_raw_file_path(result_path_in_cache, entry_number);
    struct stat st;
    if (x_stat(raw_path.c_str(), &st) != 0) {
      throw Error(
        fmt::format("Failed to stat {}: {}", raw_path, strerror(errno)));
    }
    if ((uint64_t)st.st_size != file_len) {
      throw Error(
        fmt::format("Bad file size of {} (actual {} bytes, expected {} bytes)",
                    raw_path,
                    st.st_size,
                    file_len));
    }

    const auto it = result_file_map->find(suffix_str);
    if (it != result_file_map->end()) {
      const auto& dest_path = it->second;
      if (!copy_raw_file(raw_path, dest_path, false)) {
        throw Error(
          fmt::format("Failed to copy raw file {} to {}", raw_path, dest_path));
      }
      // Update modification timestamp to save the file from LRU cleanup
      // (and, if hard-linked, to make the object file newer than the source
      // file).
      update_mtime(raw_path.c_str());
    }
  }
}

static bool
read_result(const std::string& path,
            const ResultFileMap* result_file_map,
            FILE* dump_stream)
{
  File file(path, "rb");
  if (!file) {
    // Cache miss.
    return false;
  }

  CacheEntryReader reader(file.get(), k_result_magic, k_result_version);

  if (dump_stream) {
    reader.dump_header(dump_stream);
  }

  uint8_t n_entries;
  reader.read(n_entries);

  uint32_t i;
  for (i = 0; i < n_entries; ++i) {
    uint8_t marker;
    reader.read(marker);

    ReadEntryFunction read_entry;

    switch (marker) {
    case k_embedded_file_marker:
      read_entry = read_embedded_file_entry;
      break;

    case k_raw_file_marker:
      read_entry = read_raw_file_entry;
      break;

    default:
      throw Error(fmt::format("Unknown entry type: {}", marker));
    }

    read_entry(reader, path, i, result_file_map, dump_stream);
  }

  if (i != n_entries) {
    throw Error(
      fmt::format("Too few entries (read {}, expected {})", i, n_entries));
  }

  reader.finalize();
  return true;
}

static void
write_embedded_file_entry(CacheEntryWriter& writer,
                          const std::string& /*result_path_in_cache*/,
                          uint32_t entry_number,
                          const ResultFileMap::value_type& suffix_and_path)
{
  const auto& suffix = suffix_and_path.first;
  const auto& source_path = suffix_and_path.second;

  uint64_t source_file_size;
  if (!Util::get_file_size(source_path, source_file_size)) {
    throw Error(
      fmt::format("Failed to stat {}: {}", source_path, strerror(errno)));
  }

  cc_log("Storing embedded file #%u %s (%llu bytes) from %s",
         entry_number,
         suffix.c_str(),
         (unsigned long long)source_file_size,
         source_path.c_str());

  writer.write<uint8_t>(k_embedded_file_marker);
  writer.write<uint8_t>(suffix.length());
  writer.write(suffix.data(), suffix.length());
  writer.write(source_file_size);

  File file(source_path, "rb");
  if (!file) {
    throw Error(fmt::format("Failed to open {} for reading", source_path));
  }

  uint64_t remain = source_file_size;
  while (remain > 0) {
    uint8_t buf[READ_BUFFER_SIZE];
    size_t n = std::min(remain, static_cast<uint64_t>(sizeof(buf)));
    if (fread(buf, n, 1, file.get()) != 1) {
      throw Error(fmt::format("Error reading from {}", source_path));
    }
    writer.write(buf, n);
    remain -= n;
  }
}

static void
write_raw_file_entry(CacheEntryWriter& writer,
                     const std::string& result_path_in_cache,
                     uint32_t entry_number,
                     const ResultFileMap::value_type& suffix_and_path)
{
  const auto& suffix = suffix_and_path.first;
  const auto& source_path = suffix_and_path.second;

  uint64_t source_file_size;
  if (!Util::get_file_size(source_path, source_file_size)) {
    throw Error(
      fmt::format("Failed to stat {}: {}", source_path, strerror(errno)));
  }

  uint64_t old_size;
  uint64_t new_size;

  cc_log("Storing raw file #%u %s (%llu bytes) from %s",
         entry_number,
         suffix.c_str(),
         (unsigned long long)source_file_size,
         source_path.c_str());

  writer.write<uint8_t>(k_raw_file_marker);
  writer.write<uint8_t>(suffix.length());
  writer.write(suffix.data(), suffix.length());
  writer.write(source_file_size);

  auto raw_file = get_raw_file_path(result_path_in_cache, entry_number);
  struct stat old_stat;
  bool old_existed = stat(raw_file.c_str(), &old_stat) == 0;
  if (!copy_raw_file(source_path, raw_file, true)) {
    throw Error(
      fmt::format("Failed to store {} as raw file {}", source_path, raw_file));
  }
  struct stat new_stat;
  bool new_exists = stat(raw_file.c_str(), &new_stat) == 0;

  old_size = old_existed ? file_size_on_disk(&old_stat) : 0;
  new_size = new_exists ? file_size_on_disk(&new_stat) : 0;
  stats_update_size(stats_file,
                    new_size - old_size,
                    (new_exists ? 1 : 0) - (old_existed ? 1 : 0));
}

static bool
should_store_raw_file(const std::string& suffix)
{
  if (!g_config.file_clone() && !g_config.hard_link()) {
    return false;
  }

  // - Don't store stderr outputs as raw files since they:
  //   1. Never are large.
  //   2. Will end up in a temporary file anyway.
  //
  // - Don't store .d files since they:
  //   1. Never are large.
  //   2. Compress well.
  //   3. Cause trouble for automake if hard-linked (see ccache issue 378).
  //
  // Note that .d files can't be stored as raw files for the file_clone case
  // since the hard link mode happily will try to use them if they exist. This
  // could be fixed by letting read_raw_file_entry refuse to hard link .d
  // files, but it's easier to simply always store them embedded. This will
  // also save i-nodes in the cache.
  return suffix != k_result_stderr_name && suffix != ".d";
}

static void
write_result(const std::string& path, const ResultFileMap& result_file_map)
{
  uint64_t payload_size = 0;
  payload_size += 1; // n_entries
  for (const auto& pair : result_file_map) {
    const auto& suffix = pair.first;
    const auto& result_file = pair.second;
    uint64_t source_file_size;
    if (!Util::get_file_size(result_file, source_file_size)) {
      throw Error(
        fmt::format("Failed to stat {}: {}", result_file, strerror(errno)));
    }
    payload_size += 1;                // embedded_file_marker
    payload_size += 1;                // suffix_len
    payload_size += suffix.length();  // suffix
    payload_size += 8;                // data_len
    payload_size += source_file_size; // data
  }

  AtomicFile atomic_result_file(path, AtomicFile::Mode::binary);
  CacheEntryWriter writer(atomic_result_file.stream(),
                          k_result_magic,
                          k_result_version,
                          Compression::type_from_config(),
                          Compression::level_from_config(),
                          payload_size);

  writer.write<uint8_t>(result_file_map.size());

  size_t entry_number = 0;
  for (const auto& pair : result_file_map) {
    const auto& suffix = pair.first;
    WriteEntryFunction write_entry = should_store_raw_file(suffix)
                                       ? write_raw_file_entry
                                       : write_embedded_file_entry;
    write_entry(writer, path, entry_number, pair);
    ++entry_number;
  }

  writer.finalize();
  atomic_result_file.commit();
}

bool
result_get(const std::string& path, const ResultFileMap& result_file_map)
{
  cc_log("Getting result %s", path.c_str());

  try {
    bool cache_hit = read_result(path, &result_file_map, nullptr);
    if (cache_hit) {
      // Update modification timestamp to save files from LRU cleanup.
      update_mtime(path.c_str());
    } else {
      cc_log("No such result file");
    }
    return cache_hit;
  } catch (const Error& e) {
    cc_log("Error: %s", e.what());
    return false;
  }
}

bool
result_put(const std::string& path, const ResultFileMap& result_file_map)
{
  cc_log("Storing result %s", path.c_str());

  try {
    write_result(path, result_file_map);
    return true;
  } catch (const Error& e) {
    cc_log("Error: %s", e.what());
    return false;
  }
}

bool
result_dump(const std::string& path, FILE* stream)
{
  assert(stream);

  try {
    if (read_result(path, nullptr, stream)) {
      return true;
    } else {
      fmt::print(stream, "Error: No such file: {}\n", path);
    }
  } catch (const Error& e) {
    fmt::print(stream, "Error: {}\n", e.what());
  }

  return false;
}
