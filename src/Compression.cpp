// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "Compression.hpp"

#include "Config.hpp"
#include "Context.hpp"
#include "exceptions.hpp"

namespace Compression {

int8_t
level_from_config(const Config& config)
{
  return config.compression() ? config.compression_level() : 0;
}

Type
type_from_config(const Config& config)
{
  return config.compression() ? Type::zstd : Type::none;
}

Type
type_from_int(uint8_t type)
{
  switch (type) {
  case static_cast<uint8_t>(Type::none):
    return Type::none;

  case static_cast<uint8_t>(Type::zstd):
    return Type::zstd;
  }

  throw Error(fmt::format("Unknown type: {}", type));
}

std::string
type_to_string(Type type)
{
  switch (type) {
  case Type::none:
    return "none";

  case Type::zstd:
    return "zstd";
  }

  assert(false);
  return {};
}

} // namespace Compression
