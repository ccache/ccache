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

#pragma once

#include <ccache/core/result.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace core {

// This class extracts the parts of a result entry to a directory.
class ResultExtractor : public result::Deserializer::Visitor
{
public:
  using GetRawFilePathFunction = std::function<std::filesystem::path(uint8_t)>;

  //`result_path` should be the path to the local result entry file if the
  // result comes from local storage.
  ResultExtractor(
    const std::filesystem::path& output_directory,
    std::optional<GetRawFilePathFunction> get_raw_file_path = std::nullopt);

  void on_embedded_file(uint8_t file_number,
                        result::FileType file_type,
                        nonstd::span<const uint8_t> data) override;
  void on_raw_file(uint8_t file_number,
                   result::FileType file_type,
                   uint64_t file_size) override;

private:
  std::filesystem::path m_output_directory;
  std::optional<GetRawFilePathFunction> m_get_raw_file_path;
};

} // namespace core
