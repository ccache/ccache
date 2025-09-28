// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "types.hpp"

#include <ccache/config.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/format.hpp>

namespace core {

std::string
to_string(const CacheEntryType type)
{
  switch (type) {
  case CacheEntryType::manifest:
    return "manifest";

  case CacheEntryType::result:
    return "result";

  default:
    return "unknown";
  }
}

int8_t
compression_level_from_config(const Config& config)
{
  return config.compression() ? config.compression_level() : 0;
}

CompressionType
compression_type_from_config(const Config& config)
{
  return config.compression() ? CompressionType::zstd : CompressionType::none;
}

CompressionType
compression_type_from_int(const uint8_t type)
{
  switch (type) {
  case static_cast<uint8_t>(CompressionType::none):
    return CompressionType::none;

  case static_cast<uint8_t>(CompressionType::zstd):
    return CompressionType::zstd;
  }

  throw core::Error(FMT("Unknown type: {}", type));
}

std::string
to_string(const CompressionType type)
{
  switch (type) {
  case CompressionType::none:
    return "none";

  case CompressionType::zstd:
    return "zstd";
  }

  ASSERT(false);
}

} // namespace core
