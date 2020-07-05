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

#pragma once

#include "system.hpp"

#include "third_party/nonstd/optional.hpp"

#include <map>
#include <string>
#include <vector>

class CacheEntryReader;
class CacheEntryWriter;
class Context;

namespace Result {

extern const uint8_t k_magic[4];
extern const uint8_t k_version;
extern const char* const k_unknown_file_type;

using UnderlyingFileTypeInt = uint8_t;
enum class FileType : UnderlyingFileTypeInt {
  // These values are written into the cache result file. This means they must
  // never be changed or removed unless the result file version is incremented.
  // Adding new values is OK.
  object = 0,
  dependency = 1,
  stderr_output = 2,
  coverage = 3,
  stackusage = 4,
  diagnostic = 5,
  dwarf_object = 6,
};

const char* file_type_to_string(FileType type);

// This class knows how to read a result cache entry.
class Reader
{
public:
  Reader(const std::string& result_path);

  class Consumer
  {
  public:
    virtual ~Consumer() = default;

    virtual void on_header(CacheEntryReader& cache_entry_reader) = 0;
    virtual void on_entry_start(uint32_t entry_number,
                                FileType file_type,
                                uint64_t file_len,
                                nonstd::optional<std::string> raw_file) = 0;
    virtual void on_entry_data(const uint8_t* data, size_t size) = 0;
    virtual void on_entry_end() = 0;
  };

  // Returns error message on error, otherwise nonstd::nullopt.
  nonstd::optional<std::string> read(Consumer& consumer);

private:
  const std::string m_result_path;

  bool read_result(Consumer& consumer);
  void read_entry(CacheEntryReader& cache_entry_reader,
                  uint32_t entry_number,
                  Reader::Consumer& consumer);
};

// This class knows how to write a result cache entry.
class Writer
{
public:
  Writer(Context& ctx, const std::string& result_path);
  ~Writer();

  // Register a file to include in the result. Does not throw.
  void write(FileType file_type, const std::string& file_path);

  // Write registered files to the result. Returns an error message on error.
  nonstd::optional<std::string> finalize();

private:
  Context& m_ctx;
  const std::string m_result_path;
  bool m_finalized = false;
  std::vector<std::pair<FileType, std::string>> m_entries_to_write;

  void do_finalize();
  void write_embedded_file_entry(CacheEntryWriter& writer,
                                 const std::string& path,
                                 uint64_t file_size);
  void write_raw_file_entry(const std::string& path, uint32_t entry_number);
};

} // namespace Result
