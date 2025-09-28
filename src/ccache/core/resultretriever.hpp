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

#include <ccache/core/exceptions.hpp>
#include <ccache/core/result.hpp>
#include <ccache/hash.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

class Context;

namespace core {

// This class retrieves a result entry to the local file system.
class ResultRetriever : public result::Deserializer::Visitor
{
public:
  class WriteError : public Error
  {
    using Error::Error;
  };

  //`path` should be the path to the local result entry file if the result comes
  // from local storage.
  ResultRetriever(const Context& ctx,
                  std::optional<Hash::Digest> result_key = std::nullopt);

  void on_embedded_file(uint8_t file_number,
                        result::FileType file_type,
                        nonstd::span<const uint8_t> data) override;
  void on_raw_file(uint8_t file_number,
                   result::FileType file_type,
                   uint64_t file_size) override;

private:
  const Context& m_ctx;
  std::optional<Hash::Digest> m_result_key;

  std::filesystem::path get_dest_path(result::FileType file_type) const;

  void write_dependency_file(const std::filesystem::path& path,
                             nonstd::span<const uint8_t> data);
};

} // namespace core
