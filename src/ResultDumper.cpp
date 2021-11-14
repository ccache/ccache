// Copyright (C) 2020-2021 Joel Rosdahl and other contributors
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

#include "ResultDumper.hpp"

#include "Context.hpp"
#include "Logging.hpp"
#include "fmtmacros.hpp"

#include <core/CacheEntryReader.hpp>

using nonstd::optional;

ResultDumper::ResultDumper(FILE* stream) : m_stream(stream)
{
}

void
ResultDumper::on_header(core::CacheEntryReader& cache_entry_reader,
                        const uint8_t result_format_version)
{
  cache_entry_reader.header().dump(m_stream);
  PRINT(m_stream, "Result format version: {}\n", result_format_version);
}

void
ResultDumper::on_entry_start(uint32_t entry_number,
                             Result::FileType file_type,
                             uint64_t file_len,
                             optional<std::string> raw_file)
{
  PRINT(m_stream,
        "{} file #{}: {} ({} bytes)\n",
        raw_file ? "Raw" : "Embedded",
        entry_number,
        Result::file_type_to_string(file_type),
        file_len);
}

void
ResultDumper::on_entry_data(const uint8_t* /*data*/, size_t /*size*/)
{
}

void
ResultDumper::on_entry_end()
{
}
