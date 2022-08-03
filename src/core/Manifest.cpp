// Copyright (C) 2009-2022 Joel Rosdahl and other contributors
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

#include "Manifest.hpp"

#include <Context.hpp>
#include <Hash.hpp>
#include <Logging.hpp>
#include <core/Reader.hpp>
#include <core/Writer.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <hashutil.hpp>
#include <util/XXH3_64.hpp>

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
// <digest>        ::= Digest::size() bytes
// <fsize>         ::= uint64_t ; file size
// <mtime>         ::= int64_t ; modification time
// <ctime>         ::= int64_t ; status change time
// <results>       ::= <n_results> <result>*
// <n_results>     ::= uint32_t
// <result>        ::= <n_indexes> <include_index>* <key>
// <n_indexes>     ::= uint32_t
// <include_index> ::= uint32_t
// <result_key>    ::= Digest::size() bytes

const uint32_t k_max_manifest_entries = 100;
const uint32_t k_max_manifest_file_info_entries = 10000;

namespace std {

template<> struct hash<core::Manifest::FileInfo>
{
  size_t
  operator()(const core::Manifest::FileInfo& file_info) const
  {
    static_assert(sizeof(file_info) == 48); // No padding.
    util::XXH3_64 hash;
    hash.update(&file_info, sizeof(file_info));
    return hash.digest();
  }
};

} // namespace std

namespace core {

const uint8_t Manifest::k_format_version = 0;

void
Manifest::read(Reader& reader)
{
  clear();

  const auto format_version = reader.read_int<uint8_t>();
  if (format_version != k_format_version) {
    throw core::Error(
      "Unknown format version: {} != {}", format_version, k_format_version);
  }

  const auto file_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < file_count; ++i) {
    m_files.push_back(reader.read_str(reader.read_int<uint16_t>()));
  }

  const auto file_info_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < file_info_count; ++i) {
    m_file_infos.emplace_back();
    auto& entry = m_file_infos.back();

    reader.read_int(entry.index);
    reader.read(entry.digest.bytes(), Digest::size());
    reader.read_int(entry.fsize);
    reader.read_int(entry.mtime);
    reader.read_int(entry.ctime);
  }

  const auto result_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < result_count; ++i) {
    m_results.emplace_back();
    auto& entry = m_results.back();

    const auto file_info_index_count = reader.read_int<uint32_t>();
    for (uint32_t j = 0; j < file_info_index_count; ++j) {
      entry.file_info_indexes.push_back(reader.read_int<uint32_t>());
    }
    reader.read(entry.key.bytes(), Digest::size());
  }
}

std::optional<Digest>
Manifest::look_up_result_digest(const Context& ctx) const
{
  std::unordered_map<std::string, FileStats> stated_files;
  std::unordered_map<std::string, Digest> hashed_files;

  // Check newest result first since it's a more likely to match.
  for (size_t i = m_results.size(); i > 0; i--) {
    const auto& result = m_results[i - 1];
    if (result_matches(ctx, result, stated_files, hashed_files)) {
      return result.key;
    }
  }

  return std::nullopt;
}

bool
Manifest::add_result(
  const Digest& result_key,
  const std::unordered_map<std::string, Digest>& included_files,
  const time_t time_of_compilation,
  const bool save_timestamp)
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
    file_info_indexes.push_back(get_file_info_index(path,
                                                    digest,
                                                    mf_files,
                                                    mf_file_infos,
                                                    time_of_compilation,
                                                    save_timestamp));
  }

  ResultEntry entry{std::move(file_info_indexes), result_key};
  if (std::find(m_results.begin(), m_results.end(), entry) == m_results.end()) {
    m_results.push_back(std::move(entry));
    return true;
  } else {
    return false;
  }
}

size_t
Manifest::serialized_size() const
{
  uint64_t size = 0;

  size += 1; // format_ver
  size += 4; // n_files
  for (const auto& file : m_files) {
    size += 2 + file.length();
  }
  size += 4; // n_file_infos
  size += m_file_infos.size() * (4 + Digest::size() + 8 + 8 + 8);
  size += 4; // n_results
  for (const auto& result : m_results) {
    size += 4; // n_file_info_indexes
    size += result.file_info_indexes.size() * 4;
    size += Digest::size();
  }

  return size;
}

void
Manifest::write(Writer& writer) const
{
  writer.write_int(k_format_version);
  writer.write_int<uint32_t>(m_files.size());
  for (const auto& file : m_files) {
    writer.write_int<uint16_t>(file.length());
    writer.write_str(file);
  }

  writer.write_int<uint32_t>(m_file_infos.size());
  for (const auto& file_info : m_file_infos) {
    writer.write_int<uint32_t>(file_info.index);
    writer.write(file_info.digest.bytes(), Digest::size());
    writer.write_int(file_info.fsize);
    writer.write_int(file_info.mtime);
    writer.write_int(file_info.ctime);
  }

  writer.write_int<uint32_t>(m_results.size());
  for (const auto& result : m_results) {
    writer.write_int<uint32_t>(result.file_info_indexes.size());
    for (auto index : result.file_info_indexes) {
      writer.write_int(index);
    }
    writer.write(result.key.bytes(), Digest::size());
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

uint32_t
Manifest::get_file_info_index(
  const std::string& path,
  const Digest& digest,
  const std::unordered_map<std::string, uint32_t>& mf_files,
  const std::unordered_map<FileInfo, uint32_t>& mf_file_infos,
  const time_t time_of_compilation,
  const bool save_timestamp)
{
  FileInfo fi;

  const auto f_it = mf_files.find(path);
  if (f_it != mf_files.end()) {
    fi.index = f_it->second;
  } else {
    m_files.push_back(path);
    fi.index = m_files.size() - 1;
  }

  fi.digest = digest;

  // file_stat.{m,c}time() have a resolution of 1 second, so we can cache the
  // file's mtime and ctime only if they're at least one second older than
  // time_of_compilation.
  //
  // file_stat.ctime() may be 0, so we have to check time_of_compilation against
  // MAX(mtime, ctime).
  //
  // ccache only reads mtime/ctime if file_stat_match sloppiness is enabled, so
  // mtimes/ctimes are stored as a dummy value (-1) if not enabled. This reduces
  // the number of file_info entries for the common case.

  const auto file_stat = Stat::stat(path, Stat::OnError::log);
  if (file_stat) {
    if (save_timestamp
        && time_of_compilation
             > std::max(file_stat.mtime(), file_stat.ctime())) {
      fi.mtime = file_stat.mtime();
      fi.ctime = file_stat.ctime();
    } else {
      fi.mtime = -1;
      fi.ctime = -1;
    }
    fi.fsize = file_stat.size();
  } else {
    fi.mtime = -1;
    fi.ctime = -1;
    fi.fsize = 0;
  }

  const auto fi_it = mf_file_infos.find(fi);
  if (fi_it != mf_file_infos.end()) {
    return fi_it->second;
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
  std::unordered_map<std::string, Digest>& hashed_files) const
{
  for (uint32_t file_info_index : result.file_info_indexes) {
    const auto& fi = m_file_infos[file_info_index];
    const auto& path = m_files[fi.index];

    auto stated_files_iter = stated_files.find(path);
    if (stated_files_iter == stated_files.end()) {
      auto file_stat = Stat::stat(path, Stat::OnError::log);
      if (!file_stat) {
        return false;
      }
      FileStats st;
      st.size = file_stat.size();
      st.mtime = file_stat.mtime();
      st.ctime = file_stat.ctime();
      stated_files_iter = stated_files.emplace(path, st).first;
    }
    const FileStats& fs = stated_files_iter->second;

    if (fi.fsize != fs.size) {
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

    if (ctx.config.sloppiness().is_enabled(core::Sloppy::file_stat_matches)) {
      if (!(ctx.config.sloppiness().is_enabled(
            core::Sloppy::file_stat_matches_ctime))) {
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
      Digest actual_digest;
      int ret = hash_source_code_file(ctx, actual_digest, path, fs.size);
      if (ret & HASH_SOURCE_CODE_ERROR) {
        LOG("Failed hashing {}", path);
        return false;
      }
      if (ret & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }

      hashed_files_iter = hashed_files.emplace(path, actual_digest).first;
    }

    if (fi.digest != hashed_files_iter->second) {
      return false;
    }
  }

  return true;
}

void
Manifest::dump(FILE* const stream) const
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
    PRINT(stream, "    Hash: {}\n", m_file_infos[i].digest.to_string());
    PRINT(stream, "    File size: {}\n", m_file_infos[i].fsize);
    PRINT(stream, "    Mtime: {}\n", m_file_infos[i].mtime);
    PRINT(stream, "    Ctime: {}\n", m_file_infos[i].ctime);
  }

  PRINT(stream, "Results ({}):\n", m_results.size());
  for (size_t i = 0; i < m_results.size(); ++i) {
    PRINT(stream, "  {}:\n", i);
    PRINT_RAW(stream, "    File info indexes:");
    for (uint32_t file_info_index : m_results[i].file_info_indexes) {
      PRINT(stream, " {}", file_info_index);
    }
    PRINT_RAW(stream, "\n");
    PRINT(stream, "    Key: {}\n", m_results[i].key.to_string());
  }
}

} // namespace core
