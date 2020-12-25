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

#include "ResultExtractor.hpp"

#include "Util.hpp"
#include "core/fmtmacros.hpp"

ResultExtractor::ResultExtractor(const std::string& directory)
  : m_directory(directory)
{
}

void
ResultExtractor::on_header(CacheEntryReader& /*cache_entry_reader*/)
{
}

void
ResultExtractor::on_entry_start(uint32_t /*entry_number*/,
                                Result::FileType file_type,
                                uint64_t /*file_len*/,
                                nonstd::optional<std::string> raw_file)
{
  std::string suffix = Result::file_type_to_string(file_type);
  if (suffix == Result::k_unknown_file_type) {
    suffix = FMT(".type_{}", file_type);
  } else if (suffix[0] == '<') {
    suffix[0] = '.';
    suffix.resize(suffix.length() - 1);
  }

  m_dest_path = FMT("{}/ccache-result{}", m_directory, suffix);

  if (!raw_file) {
    m_dest_fd = Fd(
      open(m_dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!m_dest_fd) {
      throw Error(
        "Failed to open {} for writing: {}", m_dest_path, strerror(errno));
    }
  } else {
    try {
      Util::copy_file(*raw_file, m_dest_path, false);
    } catch (Error& e) {
      throw Error(
        "Failed to copy {} to {}: {}", *raw_file, m_dest_path, e.what());
    }
  }
}

void
ResultExtractor::on_entry_data(const uint8_t* data, size_t size)
{
  ASSERT(m_dest_fd);

  try {
    Util::write_fd(*m_dest_fd, data, size);
  } catch (Error& e) {
    throw Error("Failed to write to {}: {}", m_dest_path, e.what());
  }
}

void
ResultExtractor::on_entry_end()
{
  if (m_dest_fd) {
    m_dest_fd.close();
  }
}
