// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "Fd.hpp"
#include "Result.hpp"

#include <core/exceptions.hpp>

class Context;

// This class retrieves a result entry to the local file system.
class ResultRetriever : public Result::Reader::Consumer
{
public:
  class WriteError : public core::Error
  {
    using core::Error::Error;
  };

  ResultRetriever(Context& ctx);

  void on_entry_start(uint8_t entry_number,
                      Result::FileType file_type,
                      uint64_t file_len,
                      std::optional<std::string> raw_file) override;
  void on_entry_data(const uint8_t* data, size_t size) override;
  void on_entry_end() override;

private:
  Context& m_ctx;
  Result::FileType m_dest_file_type{};
  Fd m_dest_fd;
  std::string m_dest_path;

  // Collects the full data of stderr output (since we want to potentially strip
  // color codes which could span chunk boundaries) or dependency data (since we
  // potentially want to rewrite the dependency target which in theory can span
  // a chunk boundary).
  std::string m_dest_data;

  void write_dependency_file();
};
