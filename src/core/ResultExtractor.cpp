// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include <core/exceptions.hpp>
#include <util/Bytes.hpp>
#include <util/DirEntry.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/fmtmacros.hpp>
#include <util/wincompat.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>

using util::DirEntry;

namespace core {

ResultExtractor::ResultExtractor(
  const std::string& output_directory,
  std::optional<GetRawFilePathFunction> get_raw_file_path)
  : m_output_directory(output_directory),
    m_get_raw_file_path(get_raw_file_path)
{
}

void
ResultExtractor::on_embedded_file(uint8_t /*file_number*/,
                                  Result::FileType file_type,
                                  nonstd::span<const uint8_t> data)
{
  std::string suffix = Result::file_type_to_string(file_type);
  if (suffix == Result::k_unknown_file_type) {
    suffix =
      FMT(".type_{}", static_cast<Result::UnderlyingFileTypeInt>(file_type));
  } else if (suffix[0] == '<') {
    suffix[0] = '.';
    suffix.resize(suffix.length() - 1);
  }

  const auto dest_path = FMT("{}/ccache-result{}", m_output_directory, suffix);
  util::throw_on_error<Error>(util::write_file(dest_path, data),
                              FMT("Failed to write to {}: ", dest_path));
}

void
ResultExtractor::on_raw_file(uint8_t file_number,
                             Result::FileType file_type,
                             uint64_t file_size)
{
  if (!m_get_raw_file_path) {
    throw Error("Raw entry for non-local result");
  }
  const auto raw_file_path = (*m_get_raw_file_path)(file_number);
  DirEntry entry(raw_file_path, DirEntry::LogOnError::yes);
  if (!entry) {
    throw Error(FMT(
      "Failed to stat {}: {}", raw_file_path, strerror(entry.error_number())));
  }
  if (entry.size() != file_size) {
    throw Error(FMT("Bad file size of {} (actual {} bytes, expected {} bytes)",
                    raw_file_path,
                    entry.size(),
                    file_size));
  }

  const auto data = util::value_or_throw<Error>(
    util::read_file<util::Bytes>(raw_file_path, file_size),
    FMT("Failed to read {}: ", raw_file_path));
  on_embedded_file(file_number, file_type, data);
}

} // namespace core
