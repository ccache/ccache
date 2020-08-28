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

#include "Result.hpp"

// This class dumps information about the result entry to `stream`.
class ResultDumper : public Result::Reader::Consumer
{
public:
  ResultDumper(FILE* stream);

  void on_header(CacheEntryReader& cache_entry_reader) override;
  void on_entry_start(uint32_t entry_number,
                      Result::FileType file_type,
                      uint64_t file_len,
                      nonstd::optional<std::string> raw_file) override;
  void on_entry_data(const uint8_t* data, size_t size) override;
  void on_entry_end() override;

private:
  FILE* m_stream;
};
