// Copyright (C) 2009-2025 Joel Rosdahl and other contributors
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

#include "manifest.hpp"

#include <ccache/context.hpp>
#include <ccache/core/cacheentrydatareader.hpp>
#include <ccache/core/cacheentrydatawriter.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/hash.hpp>
#include <ccache/hashutil.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/xxh3_64.hpp>

// Manifest data format
// ====================
//
// Integers are big-endian.
//
// <payload>       ::= <format_ver> <paths> <includes> <results>
// <format_ver>    ::= uint8_t
// <paths>         ::= <n_paths> <path_entry>*
// <n_paths>       ::= uint32_t
// <path_entry>    ::= <path_len> <path>
// <path_len>      ::= uint16_t
// <path>          ::= path_len bytes
// <includes>      ::= <n_includes> <include_entry>*
// <n_includes>    ::= uint32_t
// <include_entry> ::= <path_index> <digest> <fsize> <mtime> <ctime>
// <path_index>    ::= uint32_t
// <digest>        ::= Hash::Digest::size() bytes
// <fsize>         ::= uint64_t ; file size
// <mtime>         ::= int64_t ; modification time (ns), 0 = not recorded
// <ctime>         ::= int64_t ; status change time (ns), 0 = not recorded
// <results>       ::= <n_results> <result>*
// <n_results>     ::= uint32_t
// <result>        ::= <n_indexes> <include_index>* <key>
// <n_indexes>     ::= uint32_t
// <include_index> ::= uint32_t
// <result_key>    ::= Hash::Digest::size() bytes

const uint32_t k_max_manifest_entries = 100;
const uint32_t k_max_manifest_file_info_entries = 10000;

namespace std {

template<> struct hash<core::Manifest::FileInfo>
{
  size_t
  operator()(const core::Manifest::FileInfo& file_info) const
  {
    static_assert(sizeof(file_info) == 48); // No padding.
    util::XXH3_64 h;
    h.update(&file_info, sizeof(file_info));
    return h.digest();
  }
};

} // namespace std

namespace core {

// Format version history:
//
// Version 0:
//   - First version.
// Version 1:
//   - mtime and ctime are now stored with nanoseconds resolution.
const uint8_t Manifest::k_format_version = 1;

void
Manifest::read(nonstd::span<const uint8_t> data)
{
  std::vector<std::string> files;
  std::vector<FileInfo> file_infos;
  std::vector<ResultEntry> results;

  core::CacheEntryDataReader reader(data);

  const auto format_version = reader.read_int<uint8_t>();
  if (format_version != k_format_version) {
    throw core::Error(FMT("Unknown manifest format version: {} != {}",
                          format_version,
                          k_format_version));
  }

  const auto file_count = reader.read_int<uint32_t>();
  files.reserve(file_count);
  for (uint32_t i = 0; i < file_count; ++i) {
    files.emplace_back(reader.read_str(reader.read_int<uint16_t>()));
  }

  const auto file_info_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < file_info_count; ++i) {
    file_infos.emplace_back();
    auto& entry = file_infos.back();

    reader.read_int(entry.index);
    reader.read_and_copy_bytes(entry.digest);
    reader.read_int(entry.fsize);
    entry.mtime =
      util::TimePoint(std::chrono::nanoseconds(reader.read_int<int64_t>()));
    entry.ctime =
      util::TimePoint(std::chrono::nanoseconds(reader.read_int<int64_t>()));
  }

  const auto result_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < result_count; ++i) {
    results.emplace_back();
    auto& entry = results.back();

    const auto file_info_index_count = reader.read_int<uint32_t>();
    for (uint32_t j = 0; j < file_info_index_count; ++j) {
      entry.file_info_indexes.push_back(reader.read_int<uint32_t>());
    }
    reader.read_and_copy_bytes(entry.key);
  }

  if (m_results.empty()) {
    m_files = std::move(files);
    m_file_infos = std::move(file_infos);
    m_results = std::move(results);
  } else {
    for (const auto& result : results) {
      std::unordered_map<std::string, Hash::Digest> included_files;
      std::unordered_map<std::string, FileStats> included_files_stats;
      for (auto file_info_index : result.file_info_indexes) {
        const auto& file_info = file_infos[file_info_index];
        included_files.emplace(files[file_info.index], file_info.digest);
        included_files_stats.emplace(
          files[file_info.index],
          FileStats{file_info.fsize, file_info.mtime, file_info.ctime});
      }
      add_result(result.key, included_files, [&](const std::string& path) {
        return included_files_stats[path];
      });
    }
  }
}

std::optional<Hash::Digest>
Manifest::look_up_result_digest(const Context& ctx) const
{
  std::unordered_map<std::string, FileStats> stated_files;
  std::unordered_map<std::string, Hash::Digest> hashed_files;

  // Check newest result first since it's more likely to match.
  for (size_t i = m_results.size(); i > 0; i--) {
    const auto& result = m_results[i - 1];
    LOG("Considering result entry {} ({})",
        i - 1,
        util::format_digest(result.key));
    if (result_matches(ctx, result, stated_files, hashed_files)) {
      LOG("Result entry {} matched in manifest", i - 1);
      return result.key;
    }
  }

  return std::nullopt;
}

bool
Manifest::add_result(
  const Hash::Digest& result_key,
  const std::unordered_map<std::string, Hash::Digest>& included_files,
  const FileStater& stat_file_function)
{
  if (m_results.size() > k_max_manifest_entries) {
    // Normally, there shouldn't be many result entries in the manifest since
    // new entries are added only if an include file has changed but not the
    // source file, and you typically change source files more often than header
    // files. However, it's certainly possible to imagine cases where the
    // manifest will grow large (for instance, a generated header file that
    // changes for every build), and this must be taken care of since processing
    // an ever growing manifest eventually will take too much time. A good way
    // of solving this would be to maintain the result entries in LRU order and
    // discarding the old ones. An easy way is to throw away all entries when
    // there are too many. Let's do that for now.
    LOG("More than {} entries in manifest file; discarding",
        k_max_manifest_entries);
    clear();
  } else if (m_file_infos.size() > k_max_manifest_file_info_entries) {
    // Rarely, FileInfo entries can grow large in pathological cases where many
    // included files change, but the main file does not. This also puts an
    // upper bound on the number of FileInfo entries.
    LOG("More than {} FileInfo entries in manifest file; discarding",
        k_max_manifest_file_info_entries);
    clear();
  }

  std::unordered_map<std::string, uint32_t /*index*/> mf_files;
  for (uint32_t i = 0; i < m_files.size(); ++i) {
    mf_files.emplace(m_files[i], i);
  }

  std::unordered_map<FileInfo, uint32_t /*index*/> mf_file_infos;
  for (uint32_t i = 0; i < m_file_infos.size(); ++i) {
    mf_file_infos.emplace(m_file_infos[i], i);
  }

  std::vector<uint32_t> file_info_indexes;
  file_info_indexes.reserve(included_files.size());

  for (const auto& [path, digest] : included_files) {
    auto index = get_file_info_index(
      path, digest, mf_files, mf_file_infos, stat_file_function);
    if (!index) {
      LOG_RAW("Index overflow in manifest");
      return false;
    }
    file_info_indexes.push_back(*index);
  }

  ResultEntry entry{std::move(file_info_indexes), result_key};
  if (std::find(m_results.begin(), m_results.end(), entry) == m_results.end()) {
    m_results.push_back(std::move(entry));
    return true;
  } else {
    return false;
  }
}

uint32_t
Manifest::serialized_size() const
{
  uint64_t size = 0;

  size += 1; // format_ver
  size += 4; // n_files
  for (const auto& file : m_files) {
    size += 2 + file.length();
  }
  size += 4; // n_file_infos
  size +=
    m_file_infos.size() * (4 + std::tuple_size<Hash::Digest>() + 8 + 8 + 8);
  size += 4; // n_results
  for (const auto& result : m_results) {
    size += 4; // n_file_info_indexes
    size += result.file_info_indexes.size() * 4;
    size += std::tuple_size<Hash::Digest>();
  }

  // In order to support 32-bit ccache builds, restrict size to uint32_t for
  // now. This restriction can be lifted when we drop 32-bit support.
  const auto max = std::numeric_limits<uint32_t>::max();
  if (size > max) {
    throw core::Error(
      FMT("Serialized manifest too large ({} > {})", size, max));
  }
  return static_cast<uint32_t>(size);
}

void
Manifest::serialize(util::Bytes& output)
{
  core::CacheEntryDataWriter writer(output);

  writer.write_int(k_format_version);
  writer.write_int(static_cast<uint32_t>(m_files.size()));
  for (const auto& file : m_files) {
    writer.write_int(static_cast<uint16_t>(file.length()));
    writer.write_str(file);
  }

  writer.write_int(static_cast<uint32_t>(m_file_infos.size()));
  for (const auto& file_info : m_file_infos) {
    writer.write_int<uint32_t>(file_info.index);
    writer.write_bytes(file_info.digest);
    writer.write_int(file_info.fsize);
    writer.write_int(util::nsec_tot(file_info.mtime));
    writer.write_int(util::nsec_tot(file_info.ctime));
  }

  writer.write_int(static_cast<uint32_t>(m_results.size()));
  for (const auto& result : m_results) {
    writer.write_int(static_cast<uint32_t>(result.file_info_indexes.size()));
    for (auto index : result.file_info_indexes) {
      writer.write_int(index);
    }
    writer.write_bytes(result.key);
  }
}

bool
Manifest::FileInfo::operator==(const FileInfo& other) const
{
  return index == other.index && digest == other.digest && fsize == other.fsize
         && mtime == other.mtime && ctime == other.ctime;
}

bool
Manifest::ResultEntry::operator==(const ResultEntry& other) const
{
  return file_info_indexes == other.file_info_indexes && key == other.key;
}

void
Manifest::clear()
{
  m_files.clear();
  m_file_infos.clear();
  m_results.clear();
}

std::optional<uint32_t>
Manifest::get_file_info_index(
  const std::string& path,
  const Hash::Digest& digest,
  const std::unordered_map<std::string, uint32_t>& mf_files,
  const std::unordered_map<FileInfo, uint32_t>& mf_file_infos,
  const FileStater& file_stater)
{
  FileInfo fi;

  const auto f_it = mf_files.find(path);
  if (f_it != mf_files.end()) {
    fi.index = f_it->second;
  } else if (m_files.size() > UINT32_MAX) {
    return std::nullopt;
  } else {
    m_files.push_back(path);
    fi.index = static_cast<uint32_t>(m_files.size() - 1);
  }

  fi.digest = digest;

  const auto file_stat = file_stater(path);
  fi.mtime = file_stat.mtime;
  fi.ctime = file_stat.ctime;
  fi.fsize = file_stat.size;

  const auto fi_it = mf_file_infos.find(fi);
  if (fi_it != mf_file_infos.end()) {
    return fi_it->second;
  } else if (m_file_infos.size() > UINT32_MAX) {
    return std::nullopt;
  } else {
    m_file_infos.push_back(fi);
    return m_file_infos.size() - 1;
  }
}

bool
Manifest::result_matches(
  const Context& ctx,
  const ResultEntry& result,
  std::unordered_map<std::string, FileStats>& stated_files,
  std::unordered_map<std::string, Hash::Digest>& hashed_files) const
{
  for (uint32_t file_info_index : result.file_info_indexes) {
    const auto& fi = m_file_infos[file_info_index];
    const auto& path = m_files[fi.index];

    auto stated_files_iter = stated_files.find(path);
    if (stated_files_iter == stated_files.end()) {
      util::DirEntry entry(path);
      if (!entry) {
        LOG("{} is mentioned in a manifest entry but can't be read ({})",
            path,
            strerror(entry.error_number()));
        return false;
      }
      FileStats st;
      st.size = entry.size();
      st.mtime = entry.mtime();
      st.ctime = entry.ctime();
      stated_files_iter = stated_files.emplace(path, st).first;
    }
    const FileStats& fs = stated_files_iter->second;

    if (fs.size != fi.fsize) {
      LOG("Mismatch for {}: size {} != {}", path, fs.size, fi.fsize);
      return false;
    }

    // Clang stores the mtime of the included files in the precompiled header,
    // and will error out if that header is later used without rebuilding.
    if ((ctx.config.compiler_type() == CompilerType::clang
         || ctx.config.compiler_type() == CompilerType::other)
        && ctx.args_info.output_is_precompiled_header
        && !ctx.args_info.fno_pch_timestamp && fi.mtime != fs.mtime) {
      LOG("Precompiled header includes {}, which has a new mtime", path);
      return false;
    }

    if (ctx.config.sloppiness().contains(core::Sloppy::file_stat_matches)) {
      if (!ctx.config.sloppiness().contains(
            core::Sloppy::file_stat_matches_ctime)) {
        if (fi.mtime == fs.mtime && fi.ctime == fs.ctime) {
          LOG("mtime/ctime hit for {}", path);
          continue;
        } else {
          LOG("mtime/ctime miss for {}", path);
        }
      } else {
        if (fi.mtime == fs.mtime) {
          LOG("mtime hit for {}", path);
          continue;
        } else {
          LOG("mtime miss for {}", path);
        }
      }
    }

    auto hashed_files_iter = hashed_files.find(path);
    if (hashed_files_iter == hashed_files.end()) {
      Hash::Digest actual_digest;
      auto ret = hash_source_code_file(ctx, actual_digest, path, fs.size);
      if (ret.contains(HashSourceCode::error)) {
        LOG("Failed hashing {}", path);
        return false;
      }
      if (ret.contains(HashSourceCode::found_time)) {
        // hash_source_code_file has already logged.
        return false;
      }

      hashed_files_iter = hashed_files.emplace(path, actual_digest).first;
    }

    if (hashed_files_iter->second != fi.digest) {
      LOG("Mismatch for {}: hash {} != {}",
          path,
          util::format_digest(hashed_files_iter->second),
          util::format_digest(fi.digest));
      return false;
    }
  }

  return true;
}

void
Manifest::inspect(FILE* const stream) const
{
  PRINT(stream, "Manifest format version: {}\n", k_format_version);

  PRINT(stream, "File paths ({}):\n", m_files.size());
  for (size_t i = 0; i < m_files.size(); ++i) {
    PRINT(stream, "  {}: {}\n", i, m_files[i]);
  }

  PRINT(stream, "File infos ({}):\n", m_file_infos.size());
  for (size_t i = 0; i < m_file_infos.size(); ++i) {
    PRINT(stream, "  {}:\n", i);
    PRINT(stream, "    Path index: {}\n", m_file_infos[i].index);
    PRINT(
      stream, "    Hash: {}\n", util::format_digest(m_file_infos[i].digest));
    PRINT(stream, "    File size: {}\n", m_file_infos[i].fsize);
    if (m_file_infos[i].mtime == util::TimePoint()) {
      PRINT_RAW(stream, "    Mtime: -\n");
    } else {
      PRINT(stream,
            "    Mtime: {}.{:09}\n",
            util::sec(m_file_infos[i].mtime),
            util::nsec_part(m_file_infos[i].mtime));
    }
    if (m_file_infos[i].ctime == util::TimePoint()) {
      PRINT_RAW(stream, "    Ctime: -\n");
    } else {
      PRINT(stream,
            "    Ctime: {}.{:09}\n",
            util::sec(m_file_infos[i].ctime),
            util::nsec_part(m_file_infos[i].ctime));
    }
  }

  PRINT(stream, "Results ({}):\n", m_results.size());
  for (size_t i = 0; i < m_results.size(); ++i) {
    PRINT(stream, "  {}:\n", i);
    PRINT_RAW(stream, "    File info indexes:");
    for (uint32_t file_info_index : m_results[i].file_info_indexes) {
      PRINT(stream, " {}", file_info_index);
    }
    PRINT_RAW(stream, "\n");
    PRINT(stream, "    Key: {}\n", util::format_digest(m_results[i].key));
  }
}

} // namespace core
