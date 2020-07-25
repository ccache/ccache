// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "Fd.hpp"
#include "Result.hpp"

class Context;

// This class retrieves a result entry to the local file system.
class ResultRetriever : public Result::Reader::Consumer
{
public:
  ResultRetriever(Context& ctx, bool rewrite_dependency_target);

  virtual void on_header(CacheEntryReader& cache_entry_reader);
  virtual void on_entry_start(uint32_t entry_number,
                              Result::FileType file_type,
                              uint64_t file_len,
                              nonstd::optional<std::string> raw_file);
  virtual void on_entry_data(const uint8_t* data, size_t size);
  virtual void on_entry_end();

private:
  Context& m_ctx;
  Result::FileType m_dest_file_type;
  Fd m_dest_fd;
  std::string m_dest_path;

  // Collects the full data of stderr output (since we want to potentially strip
  // color codes which could span chunk boundaries) or dependency data (since we
  // potentially want to rewrite the dependency target which in theory can span
  // a chunk boundary).
  std::string m_dest_data;

  // Whether to rewrite the first part of the dependency file data to the
  // destination object file.
  bool m_rewrite_dependency_target;

  void write_dependency_file();
};
