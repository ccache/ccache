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

#include <core/Result.hpp>

#include <cstdint>
#include <cstdio>

namespace core {

// This class writes information about the result entry to `stream`.
class ResultInspector : public Result::Deserializer::Visitor
{
public:
  ResultInspector(FILE* stream);

  void on_header(const Result::Deserializer::Header& header) override;

  void on_embedded_file(uint8_t file_number,
                        Result::FileType file_type,
                        nonstd::span<const uint8_t> data) override;
  void on_raw_file(uint8_t file_number,
                   Result::FileType file_type,
                   uint64_t file_size) override;

private:
  FILE* m_stream;
};

} // namespace core
