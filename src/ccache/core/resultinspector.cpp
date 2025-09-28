// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include "resultinspector.hpp"

#include <ccache/context.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>

namespace core {

ResultInspector::ResultInspector(FILE* stream)
  : m_stream(stream)
{
}

void
ResultInspector::on_header(const result::Deserializer::Header& header)
{
  PRINT(m_stream, "Result format version: {}\n", header.format_version);
  PRINT(m_stream, "Number of files: {}\n", header.n_files);
}

void
ResultInspector::on_embedded_file(uint8_t file_number,
                                  result::FileType file_type,
                                  nonstd::span<const uint8_t> data)
{
  PRINT(m_stream,
        "Embedded file #{}: {} ({} bytes)\n",
        file_number,
        result::file_type_to_string(file_type),
        data.size());
}

void
ResultInspector::on_raw_file(uint8_t file_number,
                             result::FileType file_type,
                             uint64_t file_size)
{
  PRINT(m_stream,
        "Raw file #{}: {} ({} bytes)\n",
        file_number,
        result::file_type_to_string(file_type),
        file_size);
}

} // namespace core
