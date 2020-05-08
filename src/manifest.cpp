// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "manifest.hpp"

#include "AtomicFile.hpp"
#include "CacheEntryReader.hpp"
#include "CacheEntryWriter.hpp"
#include "Checksum.hpp"
#include "Config.hpp"
#include "Context.hpp"
#include "File.hpp"
#include "StdMakeUnique.hpp"
#include "ccache.hpp"
#include "hash.hpp"
#include "hashutil.hpp"
#include "logging.hpp"

// Manifest data format
// ====================
//
// Integers are big-endian.
//
// <manifest>      ::= <header> <body> <epilogue
// <header>        ::= <magic> <version> <compr_type> <compr_level>
//                     <content_len>
// <magic>         ::= 4 bytes ("cCrS")
// <version>       ::= uint8_t
// <compr_type>    ::= <compr_none> | <compr_zstd>
// <compr_none>    ::= 0 (uint8_t)
// <compr_zstd>    ::= 1 (uint8_t)
// <compr_level>   ::= int8_t
// <content_len>   ::= uint64_t ; size of file if stored uncompressed
// <body>          ::= <paths> <includes> <results> ; body is potentially
//                                                  ; compressed
// <paths>         ::= <n_paths> <path_entry>*
// <n_paths>       ::= uint32_t
// <path_entry>    ::= <path_len> <path>
// <path_len>      ::= uint16_t
// <path>          ::= path_len bytes
// <includes>      ::= <n_includes> <include_entry>*
// <n_includes>    ::= uint32_t
// <include_entry> ::= <path_index> <digest> <fsize> <mtime> <ctime>
// <path_index>    ::= uint32_t
// <digest>        ::= DIGEST_SIZE bytes
// <fsize>         ::= uint64_t ; file size
// <mtime>         ::= int64_t ; modification time
// <ctime>         ::= int64_t ; status change time
// <results>       ::= <n_results> <result>*
// <n_results>     ::= uint32_t
// <result>        ::= <n_indexes> <include_index>* <name>
// <n_indexes>     ::= uint32_t
// <include_index> ::= uint32_t
// <name>          ::= DIGEST_SIZE bytes
// <epilogue>      ::= <checksum>
// <checksum>      ::= uint64_t ; XXH64 of content bytes
//
// Sketch of concrete layout:

// <magic>         4 bytes
// <version>       1 byte
// <compr_type>    1 byte
// <compr_level>   1 byte
// <content_len>   8 bytes
// --- [potentially compressed from here] -------------------------------------
// <n_paths>       4 bytes
// <path_len>      2 bytes
// <path>          path_len bytes
// ...
// ----------------------------------------------------------------------------
// <n_includes>    4 bytes
// <path_index>    4 bytes
// <digest>        DIGEST_SIZE bytes
// <fsize>         8 bytes
// <mtime>         8 bytes
// <ctime>         8 bytes
// ...
// ----------------------------------------------------------------------------
// <n_results>     4 bytes
// <n_indexes>     4 bytes
// <include_index> 4 bytes
// ...
// <name>          DIGEST_SIZE bytes
// ...
// checksum        8 bytes
//
//
// Version history
// ===============
//
// 1: Introduced in ccache 3.0. (Files are always compressed with gzip.)
// 2: Introduced in ccache 4.0.

const uint8_t k_manifest_magic[4] = {'c', 'C', 'm', 'F'};
const uint8_t k_manifest_version = 2;
const uint32_t k_max_manifest_entries = 100;
const uint32_t k_max_manifest_file_info_entries = 10000;

namespace {

struct FileInfo
{
  // Index to n_files.
  uint32_t index;
  // Digest of referenced file.
  struct digest digest;
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
  return lhs.index == rhs.index && digests_equal(&lhs.digest, &rhs.digest)
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
    Checksum checksum;
    checksum.update(&file_info, sizeof(file_info));
    return checksum.digest();
  }
};

} // namespace std

struct ResultEntry
{
  // Indexes to file_infos.
  std::vector<uint32_t> file_info_indexes;

  // Name of the result.
  struct digest name;
};

struct ManifestData
{
  // Referenced include files.
  std::vector<std::string> files;

  // Information about referenced include files.
  std::vector<FileInfo> file_infos;

  // Result names plus references to include file infos.
  std::vector<ResultEntry> results;

  void
  add_result_entry(
    const struct digest& result_digest,
    const std::unordered_map<std::string, digest>& included_files,
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

    results.push_back(ResultEntry{std::move(file_info_indexes), result_digest});
  }

private:
  uint32_t
  get_file_info_index(
    const std::string& path,
    const digest& digest,
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

static std::unique_ptr<ManifestData>
read_manifest(const std::string& path, FILE* dump_stream = nullptr)
{
  File file(path, "rb");
  if (!file) {
    return {};
  }

  CacheEntryReader reader(file.get(), k_manifest_magic, k_manifest_version);

  if (dump_stream) {
    reader.dump_header(dump_stream);
  }

  auto mf = std::make_unique<ManifestData>();

  uint32_t entry_count;
  reader.read(entry_count);
  for (uint32_t i = 0; i < entry_count; ++i) {
    mf->files.emplace_back();
    auto& entry = mf->files.back();

    uint16_t length;
    reader.read(length);
    entry.assign(length, 0);
    reader.read(&entry[0], length);
  }

  reader.read(entry_count);
  for (uint32_t i = 0; i < entry_count; ++i) {
    mf->file_infos.emplace_back();
    auto& entry = mf->file_infos.back();

    reader.read(entry.index);
    reader.read(entry.digest.bytes, DIGEST_SIZE);
    reader.read(entry.fsize);
    reader.read(entry.mtime);
    reader.read(entry.ctime);
  }

  reader.read(entry_count);
  for (uint32_t i = 0; i < entry_count; ++i) {
    mf->results.emplace_back();
    auto& entry = mf->results.back();

    uint32_t file_info_count;
    reader.read(file_info_count);
    for (uint32_t j = 0; j < file_info_count; ++j) {
      uint32_t file_info_index;
      reader.read(file_info_index);
      entry.file_info_indexes.push_back(file_info_index);
    }
    reader.read(entry.name.bytes, DIGEST_SIZE);
  }

  reader.finalize();
  return mf;
}

static bool
write_manifest(const Config& config,
               const std::string& path,
               const ManifestData& mf)
{
  uint64_t payload_size = 0;
  payload_size += 4; // n_files
  for (const auto& file : mf.files) {
    payload_size += 2 + file.length();
  }
  payload_size += 4; // n_file_infos
  payload_size += mf.file_infos.size() * (4 + DIGEST_SIZE + 8 + 8 + 8);
  payload_size += 4; // n_results
  for (const auto& result : mf.results) {
    payload_size += 4; // n_file_info_indexes
    payload_size += result.file_info_indexes.size() * 4;
    payload_size += DIGEST_SIZE;
  }

  AtomicFile atomic_manifest_file(path, AtomicFile::Mode::binary);
  CacheEntryWriter writer(atomic_manifest_file.stream(),
                          k_manifest_magic,
                          k_manifest_version,
                          Compression::type_from_config(config),
                          Compression::level_from_config(config),
                          payload_size);
  writer.write<uint32_t>(mf.files.size());
  for (const auto& file : mf.files) {
    writer.write<uint16_t>(file.length());
    writer.write(file.data(), file.length());
  }

  writer.write<uint32_t>(mf.file_infos.size());
  for (const auto& file_info : mf.file_infos) {
    writer.write<uint32_t>(file_info.index);
    writer.write(file_info.digest.bytes, DIGEST_SIZE);
    writer.write(file_info.fsize);
    writer.write(file_info.mtime);
    writer.write(file_info.ctime);
  }

  writer.write<uint32_t>(mf.results.size());
  for (const auto& result : mf.results) {
    writer.write<uint32_t>(result.file_info_indexes.size());
    for (uint32_t j = 0; j < result.file_info_indexes.size(); ++j) {
      writer.write(result.file_info_indexes[j]);
    }
    writer.write(result.name.bytes, DIGEST_SIZE);
  }

  writer.finalize();
  atomic_manifest_file.commit();
  return true;
}

static bool
verify_result(const Context& ctx,
              const ManifestData& mf,
              const ResultEntry& result,
              std::unordered_map<std::string, FileStats>& stated_files,
              std::unordered_map<std::string, digest>& hashed_files)
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
    if ((ctx.guessed_compiler == GuessedCompiler::clang
         || ctx.guessed_compiler == GuessedCompiler::unknown)
        && ctx.args_info.output_is_precompiled_header && fi.mtime != fs.mtime) {
      cc_log("Precompiled header includes %s, which has a new mtime",
             path.c_str());
      return false;
    }

    if (ctx.config.sloppiness() & SLOPPY_FILE_STAT_MATCHES) {
      if (!(ctx.config.sloppiness() & SLOPPY_FILE_STAT_MATCHES_CTIME)) {
        if (fi.mtime == fs.mtime && fi.ctime == fs.ctime) {
          cc_log("mtime/ctime hit for %s", path.c_str());
          continue;
        } else {
          cc_log("mtime/ctime miss for %s", path.c_str());
        }
      } else {
        if (fi.mtime == fs.mtime) {
          cc_log("mtime hit for %s", path.c_str());
          continue;
        } else {
          cc_log("mtime miss for %s", path.c_str());
        }
      }
    }

    auto hashed_files_iter = hashed_files.find(path);
    if (hashed_files_iter == hashed_files.end()) {
      struct hash* hash = hash_init();
      int ret = hash_source_code_file(ctx.config, hash, path.c_str(), fs.size);
      if (ret & HASH_SOURCE_CODE_ERROR) {
        cc_log("Failed hashing %s", path.c_str());
        hash_free(hash);
        return false;
      }
      if (ret & HASH_SOURCE_CODE_FOUND_TIME) {
        hash_free(hash);
        return false;
      }

      digest actual;
      hash_result_as_bytes(hash, &actual);
      hash_free(hash);
      hashed_files_iter = hashed_files.emplace(path, actual).first;
    }

    if (!digests_equal(&fi.digest, &hashed_files_iter->second)) {
      return false;
    }
  }

  return true;
}

// Try to get the result name from a manifest file. Caller frees. Returns NULL
// on failure.
struct digest*
manifest_get(const Context& ctx, const std::string& path)
{
  std::unique_ptr<ManifestData> mf;
  try {
    mf = read_manifest(path);
    if (mf) {
      // Update modification timestamp to save files from LRU cleanup.
      update_mtime(path.c_str());
    } else {
      cc_log("No such manifest file");
      return nullptr;
    }
  } catch (const Error& e) {
    cc_log("Error: %s", e.what());
    return nullptr;
  }

  std::unordered_map<std::string, FileStats> stated_files;
  std::unordered_map<std::string, digest> hashed_files;

  // Check newest result first since it's a bit more likely to match.
  struct digest* name = nullptr;
  for (uint32_t i = mf->results.size(); i > 0; i--) {
    if (verify_result(
          ctx, *mf, mf->results[i - 1], stated_files, hashed_files)) {
      name = static_cast<digest*>(x_malloc(sizeof(digest)));
      *name = mf->results[i - 1].name;
      break;
    }
  }

  return name;
}

// Put the result name into a manifest file given a set of included files.
// Returns true on success, otherwise false.
bool
manifest_put(const Config& config,
             const std::string& path,
             const struct digest& result_name,
             const std::unordered_map<std::string, digest>& included_files,

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
  } catch (const Error& e) {
    cc_log("Error: %s", e.what());
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
    cc_log("More than %u entries in manifest file; discarding",
           k_max_manifest_entries);
    mf = std::make_unique<ManifestData>();
  } else if (mf->file_infos.size() > k_max_manifest_file_info_entries) {
    // Rarely, FileInfo entries can grow large in pathological cases where
    // many included files change, but the main file does not. This also puts
    // an upper bound on the number of FileInfo entries.
    cc_log("More than %u FileInfo entries in manifest file; discarding",
           k_max_manifest_file_info_entries);
    mf = std::make_unique<ManifestData>();
  }

  mf->add_result_entry(
    result_name, included_files, time_of_compilation, save_timestamp);

  try {
    write_manifest(config, path, *mf);
    return true;
  } catch (const Error& e) {
    cc_log("Error: %s", e.what());
    return false;
  }
}

bool
manifest_dump(const std::string& path, FILE* stream)
{
  std::unique_ptr<ManifestData> mf;
  try {
    mf = read_manifest(path, stream);
  } catch (const Error& e) {
    fmt::print(stream, "Error: {}\n", e.what());
    return false;
  }

  if (!mf) {
    fmt::print(stream, "Error: No such file: {}\n", path);
    return false;
  }

  fmt::print(stream, "File paths ({}):\n", mf->files.size());
  for (unsigned i = 0; i < mf->files.size(); ++i) {
    fmt::print(stream, "  {}: {}\n", i, mf->files[i]);
  }
  fmt::print(stream, "File infos ({}):\n", mf->file_infos.size());
  for (unsigned i = 0; i < mf->file_infos.size(); ++i) {
    char digest[DIGEST_STRING_BUFFER_SIZE];
    fmt::print(stream, "  {}:\n", i);
    fmt::print(stream, "    Path index: {}\n", mf->file_infos[i].index);
    digest_as_string(&mf->file_infos[i].digest, digest);
    fmt::print(stream, "    Hash: {}\n", digest);
    fmt::print(stream, "    File size: {}\n", mf->file_infos[i].fsize);
    fmt::print(stream, "    Mtime: {}\n", mf->file_infos[i].mtime);
    fmt::print(stream, "    Ctime: {}\n", mf->file_infos[i].ctime);
  }
  fmt::print(stream, "Results ({}):\n", mf->results.size());
  for (unsigned i = 0; i < mf->results.size(); ++i) {
    char name[DIGEST_STRING_BUFFER_SIZE];
    fmt::print(stream, "  {}:\n", i);
    fmt::print(stream, "    File info indexes:");
    for (uint32_t file_info_index : mf->results[i].file_info_indexes) {
      fmt::print(stream, " {}", file_info_index);
    }
    fmt::print(stream, "\n");
    digest_as_string(&mf->results[i].name, name);
    fmt::print(stream, "    Name: {}\n", name);
  }

  return true;
}
