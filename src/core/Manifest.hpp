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

#pragma once

#include <Digest.hpp>
#include <core/Serializer.hpp>

#include <third_party/nonstd/span.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Context;

namespace core {

class Manifest : public Serializer
{
public:
  static const uint8_t k_format_version;

  Manifest() = default;

  void read(nonstd::span<const uint8_t> data);

  std::optional<Digest> look_up_result_digest(const Context& ctx) const;

  bool add_result(const Digest& result_key,
                  const std::unordered_map<std::string, Digest>& included_files,
                  time_t time_of_compilation,
                  bool save_timestamp);

  // core::Serializer
  uint32_t serialized_size() const override;
  void serialize(util::Bytes& output) override;

  void inspect(FILE* stream) const;

private:
  struct FileStats
  {
    uint64_t size;
    int64_t mtime;
    int64_t ctime;
  };

  struct FileInfo
  {
    uint32_t index; // Index to m_files.
    Digest digest;  // Digest of referenced file.
    uint64_t fsize; // Size of referenced file.
    int64_t mtime;  // mtime of referenced file.
    int64_t ctime;  // ctime of referenced file.

    bool operator==(const FileInfo& other) const;
  };

  friend std::hash<FileInfo>;

  struct ResultEntry
  {
    std::vector<uint32_t> file_info_indexes; // Indexes to m_file_infos.
    Digest key;                              // Key of the result.

    bool operator==(const ResultEntry& other) const;
  };

  std::vector<std::string> m_files;   // Names of referenced include files.
  std::vector<FileInfo> m_file_infos; // Info about referenced include files.
  std::vector<ResultEntry> m_results;

  void clear();

  uint32_t get_file_info_index(
    const std::string& path,
    const Digest& digest,
    const std::unordered_map<std::string, uint32_t>& mf_files,
    const std::unordered_map<FileInfo, uint32_t>& mf_file_infos,
    time_t time_of_compilation,
    bool save_timestamp);

  bool
  result_matches(const Context& ctx,
                 const ResultEntry& result,
                 std::unordered_map<std::string, FileStats>& stated_files,
                 std::unordered_map<std::string, Digest>& hashed_files) const;
};

} // namespace core
