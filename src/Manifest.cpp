// Copyright (C) 2009-2021 Joel Rosdahl and other contributors
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

#include "AtomicFile.hpp"
#include "Config.hpp"
#include "Context.hpp"
#include "Digest.hpp"
#include "File.hpp"
#include "Hash.hpp"
#include "Logging.hpp"
#include "fmtmacros.hpp"
#include "hashutil.hpp"

#include <ccache.hpp>
#include <core/CacheEntryReader.hpp>
#include <core/CacheEntryWriter.hpp>
#include <core/FileReader.hpp>
#include <core/FileWriter.hpp>
#include <core/exceptions.hpp>
#include <util/XXH3_64.hpp>

#include <memory>

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

using nonstd::nullopt;
using nonstd::optional;

const uint8_t k_manifest_format_version = 0;
const uint32_t k_max_manifest_entries = 100;
const uint32_t k_max_manifest_file_info_entries = 10000;

namespace {

struct FileInfo
{
  // Index to n_files.
  uint32_t index;
  // Digest of referenced file.
  Digest digest;
  // Size of referenced file.
  uint64_t fsize;
  // mtime of referenced file.
  int64_t mtime;
  // ctime of referenced file.
  int64_t ctime;
};

bool
operator==(const FileInfo& lhs, const FileInfo& rhs)
{
  return lhs.index == rhs.index && lhs.digest == rhs.digest
         && lhs.fsize == rhs.fsize && lhs.mtime == rhs.mtime
         && lhs.ctime == rhs.ctime;
}

} // namespace

namespace std {

template<> struct hash<FileInfo>
{
  size_t
  operator()(const FileInfo& file_info) const
  {
    static_assert(sizeof(FileInfo) == 48, "unexpected size"); // No padding.
    util::XXH3_64 hash;
    hash.update(&file_info, sizeof(file_info));
    return hash.digest();
  }
};

} // namespace std

namespace {

struct ResultEntry
{
  // Indexes to file_infos.
  std::vector<uint32_t> file_info_indexes;

  // Key of the result.
  Digest key;
};

bool
operator==(const ResultEntry& lhs, const ResultEntry& rhs)
{
  return lhs.file_info_indexes == rhs.file_info_indexes && lhs.key == rhs.key;
}

struct ManifestData
{
  // Referenced include files.
  std::vector<std::string> files;

  // Information about referenced include files.
  std::vector<FileInfo> file_infos;

  // Result keys plus references to include file infos.
  std::vector<ResultEntry> results;

  bool
  add_result_entry(
    const Digest& result_key,
    const std::unordered_map<std::string, Digest>& included_files,
    time_t time_of_compilation,
    bool save_timestamp)
  {
    std::unordered_map<std::string, uint32_t /*index*/> mf_files;
    for (uint32_t i = 0; i < files.size(); ++i) {
      mf_files.emplace(files[i], i);
    }

    std::unordered_map<FileInfo, uint32_t /*index*/> mf_file_infos;
    for (uint32_t i = 0; i < file_infos.size(); ++i) {
      mf_file_infos.emplace(file_infos[i], i);
    }

    std::vector<uint32_t> file_info_indexes;
    file_info_indexes.reserve(included_files.size());

    for (const auto& item : included_files) {
      file_info_indexes.push_back(get_file_info_index(item.first,
                                                      item.second,
                                                      mf_files,
                                                      mf_file_infos,
                                                      time_of_compilation,
                                                      save_timestamp));
    }

    ResultEntry entry{std::move(file_info_indexes), result_key};
    if (std::find(results.begin(), results.end(), entry) == results.end()) {
      results.push_back(std::move(entry));
      return true;
    } else {
      return false;
    }
  }

private:
  uint32_t
  get_file_info_index(
    const std::string& path,
    const Digest& digest,
    const std::unordered_map<std::string, uint32_t>& mf_files,
    const std::unordered_map<FileInfo, uint32_t>& mf_file_infos,
    time_t time_of_compilation,
    bool save_timestamp)
  {
    struct FileInfo fi;

    auto f_it = mf_files.find(path);
    if (f_it != mf_files.end()) {
      fi.index = f_it->second;
    } else {
      files.push_back(path);
      fi.index = files.size() - 1;
    }

    fi.digest = digest;

    // file_stat.{m,c}time() have a resolution of 1 second, so we can cache the
    // file's mtime and ctime only if they're at least one second older than
    // time_of_compilation.
    //
    // file_stat.ctime() may be 0, so we have to check time_of_compilation
    // against MAX(mtime, ctime).
    //
    // ccache only reads mtime/ctime if file_stat_match sloppiness is enabled,
    // so mtimes/ctimes are stored as a dummy value (-1) if not enabled. This
    // reduces the number of file_info entries for the common case.

    auto file_stat = Stat::stat(path, Stat::OnError::log);
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

    auto fi_it = mf_file_infos.find(fi);
    if (fi_it != mf_file_infos.end()) {
      return fi_it->second;
    } else {
      file_infos.push_back(fi);
      return file_infos.size() - 1;
    }
  }
};

struct FileStats
{
  uint64_t size;
  int64_t mtime;
  int64_t ctime;
};

std::unique_ptr<ManifestData>
read_manifest(const std::string& path, FILE* dump_stream = nullptr)
{
  FILE* file_stream;
  File file;
  if (path == "-") {
    file_stream = stdin;
  } else {
    file = File(path, "rb");
    if (!file) {
      return {};
    }
    file_stream = file.get();
  }

  core::FileReader file_reader(file_stream);
  core::CacheEntryReader reader(file_reader);

  if (dump_stream) {
    reader.header().dump(dump_stream);
  }

  const auto format_ver = reader.read_int<uint8_t>();
  if (format_ver != k_manifest_format_version) {
    throw core::Error("Unknown manifest format version: {}", format_ver);
  }

  if (dump_stream) {
    PRINT(dump_stream, "Manifest format version: {}\n", format_ver);
  }

  auto mf = std::make_unique<ManifestData>();

  const auto file_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < file_count; ++i) {
    mf->files.push_back(reader.read_str(reader.read_int<uint16_t>()));
  }

  const auto file_info_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < file_info_count; ++i) {
    mf->file_infos.emplace_back();
    auto& entry = mf->file_infos.back();

    reader.read_int(entry.index);
    reader.read(entry.digest.bytes(), Digest::size());
    reader.read_int(entry.fsize);
    reader.read_int(entry.mtime);
    reader.read_int(entry.ctime);
  }

  const auto result_count = reader.read_int<uint32_t>();
  for (uint32_t i = 0; i < result_count; ++i) {
    mf->results.emplace_back();
    auto& entry = mf->results.back();

    const auto file_info_index_count = reader.read_int<uint32_t>();
    for (uint32_t j = 0; j < file_info_index_count; ++j) {
      entry.file_info_indexes.push_back(reader.read_int<uint32_t>());
    }
    reader.read(entry.key.bytes(), Digest::size());
  }

  reader.finalize();
  return mf;
}

bool
write_manifest(const Config& config,
               const std::string& path,
               const ManifestData& mf)
{
  uint64_t payload_size = 0;
  payload_size += 1; // format_ver
  payload_size += 4; // n_files
  for (const auto& file : mf.files) {
    payload_size += 2 + file.length();
  }
  payload_size += 4; // n_file_infos
  payload_size += mf.file_infos.size() * (4 + Digest::size() + 8 + 8 + 8);
  payload_size += 4; // n_results
  for (const auto& result : mf.results) {
    payload_size += 4; // n_file_info_indexes
    payload_size += result.file_info_indexes.size() * 4;
    payload_size += Digest::size();
  }

  AtomicFile atomic_manifest_file(path, AtomicFile::Mode::binary);
  core::FileWriter file_writer(atomic_manifest_file.stream());
  core::CacheEntryHeader header(core::CacheEntryType::manifest,
                                compression::type_from_config(config),
                                compression::level_from_config(config),
                                time(nullptr),
                                CCACHE_VERSION,
                                config.namespace_());
  header.set_entry_size_from_payload_size(payload_size);

  core::CacheEntryWriter writer(file_writer, header);
  writer.write_int(k_manifest_format_version);
  writer.write_int<uint32_t>(mf.files.size());
  for (const auto& file : mf.files) {
    writer.write_int<uint16_t>(file.length());
    writer.write_str(file);
  }

  writer.write_int<uint32_t>(mf.file_infos.size());
  for (const auto& file_info : mf.file_infos) {
    writer.write_int<uint32_t>(file_info.index);
    writer.write(file_info.digest.bytes(), Digest::size());
    writer.write_int(file_info.fsize);
    writer.write_int(file_info.mtime);
    writer.write_int(file_info.ctime);
  }

  writer.write_int<uint32_t>(mf.results.size());
  for (const auto& result : mf.results) {
    writer.write_int<uint32_t>(result.file_info_indexes.size());
    for (auto index : result.file_info_indexes) {
      writer.write_int(index);
    }
    writer.write(result.key.bytes(), Digest::size());
  }

  writer.finalize();
  atomic_manifest_file.commit();
  return true;
}

bool
verify_result(const Context& ctx,
              const ManifestData& mf,
              const ResultEntry& result,
              std::unordered_map<std::string, FileStats>& stated_files,
              std::unordered_map<std::string, Digest>& hashed_files)
{
  for (uint32_t file_info_index : result.file_info_indexes) {
    const auto& fi = mf.file_infos[file_info_index];
    const auto& path = mf.files[fi.index];

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
      Hash hash;
      int ret = hash_source_code_file(ctx, hash, path, fs.size);
      if (ret & HASH_SOURCE_CODE_ERROR) {
        LOG("Failed hashing {}", path);
        return false;
      }
      if (ret & HASH_SOURCE_CODE_FOUND_TIME) {
        return false;
      }

      Digest actual = hash.digest();
      hashed_files_iter = hashed_files.emplace(path, actual).first;
    }

    if (fi.digest != hashed_files_iter->second) {
      return false;
    }
  }

  return true;
}

} // namespace

namespace Manifest {

const std::string k_file_suffix = "M";
const uint8_t k_magic[4] = {'c', 'C', 'm', 'F'};
const uint8_t k_version = 2;

// Try to get the result key from a manifest file. Returns nullopt on failure.
optional<Digest>
get(const Context& ctx, const std::string& path)
{
  std::unique_ptr<ManifestData> mf;
  try {
    mf = read_manifest(path);
    if (!mf) {
      LOG_RAW("No such manifest file");
      return nullopt;
    }
  } catch (const core::Error& e) {
    LOG("Error: {}", e.what());
    return nullopt;
  }

  std::unordered_map<std::string, FileStats> stated_files;
  std::unordered_map<std::string, Digest> hashed_files;

  // Check newest result first since it's a bit more likely to match.
  for (uint32_t i = mf->results.size(); i > 0; i--) {
    if (verify_result(
          ctx, *mf, mf->results[i - 1], stated_files, hashed_files)) {
      return mf->results[i - 1].key;
    }
  }

  return nullopt;
}

// Put the result key into a manifest file given a set of included files.
// Returns true on success, otherwise false.
bool
put(const Config& config,
    const std::string& path,
    const Digest& result_key,
    const std::unordered_map<std::string, Digest>& included_files,

    time_t time_of_compilation,
    bool save_timestamp)
{
  // We don't bother to acquire a lock when writing the manifest to disk. A
  // race between two processes will only result in one lost entry, which is
  // not a big deal, and it's also very unlikely.

  std::unique_ptr<ManifestData> mf;
  try {
    mf = read_manifest(path);
    if (!mf) {
      // Manifest file didn't exist.
      mf = std::make_unique<ManifestData>();
    }
  } catch (const core::Error& e) {
    LOG("Error: {}", e.what());
    // Manifest file was corrupt, ignore.
    mf = std::make_unique<ManifestData>();
  }

  if (mf->results.size() > k_max_manifest_entries) {
    // Normally, there shouldn't be many result entries in the manifest since
    // new entries are added only if an include file has changed but not the
    // source file, and you typically change source files more often than
    // header files. However, it's certainly possible to imagine cases where
    // the manifest will grow large (for instance, a generated header file that
    // changes for every build), and this must be taken care of since
    // processing an ever growing manifest eventually will take too much time.
    // A good way of solving this would be to maintain the result entries in
    // LRU order and discarding the old ones. An easy way is to throw away all
    // entries when there are too many. Let's do that for now.
    LOG("More than {} entries in manifest file; discarding",
        k_max_manifest_entries);
    mf = std::make_unique<ManifestData>();
  } else if (mf->file_infos.size() > k_max_manifest_file_info_entries) {
    // Rarely, FileInfo entries can grow large in pathological cases where
    // many included files change, but the main file does not. This also puts
    // an upper bound on the number of FileInfo entries.
    LOG("More than {} FileInfo entries in manifest file; discarding",
        k_max_manifest_file_info_entries);
    mf = std::make_unique<ManifestData>();
  }

  bool added = mf->add_result_entry(
    result_key, included_files, time_of_compilation, save_timestamp);

  if (added) {
    try {
      write_manifest(config, path, *mf);
      return true;
    } catch (const core::Error& e) {
      LOG("Error: {}", e.what());
    }
  } else {
    LOG_RAW("The entry already exists in the manifest, not adding");
  }
  return false;
}

bool
dump(const std::string& path, FILE* stream)
{
  std::unique_ptr<ManifestData> mf;
  try {
    mf = read_manifest(path, stream);
  } catch (const core::Error& e) {
    PRINT(stream, "Error: {}\n", e.what());
    return false;
  }

  if (!mf) {
    PRINT(stream, "Error: No such file: {}\n", path);
    return false;
  }

  PRINT(stream, "File paths ({}):\n", mf->files.size());
  for (size_t i = 0; i < mf->files.size(); ++i) {
    PRINT(stream, "  {}: {}\n", i, mf->files[i]);
  }
  PRINT(stream, "File infos ({}):\n", mf->file_infos.size());
  for (size_t i = 0; i < mf->file_infos.size(); ++i) {
    PRINT(stream, "  {}:\n", i);
    PRINT(stream, "    Path index: {}\n", mf->file_infos[i].index);
    PRINT(stream, "    Hash: {}\n", mf->file_infos[i].digest.to_string());
    PRINT(stream, "    File size: {}\n", mf->file_infos[i].fsize);
    PRINT(stream, "    Mtime: {}\n", mf->file_infos[i].mtime);
    PRINT(stream, "    Ctime: {}\n", mf->file_infos[i].ctime);
  }
  PRINT(stream, "Results ({}):\n", mf->results.size());
  for (size_t i = 0; i < mf->results.size(); ++i) {
    PRINT(stream, "  {}:\n", i);
    PRINT_RAW(stream, "    File info indexes:");
    for (uint32_t file_info_index : mf->results[i].file_info_indexes) {
      PRINT(stream, " {}", file_info_index);
    }
    PRINT_RAW(stream, "\n");
    PRINT(stream, "    Key: {}\n", mf->results[i].key.to_string());
  }

  return true;
}

} // namespace Manifest
