// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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
#include <ccache/util/conversion.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>

#include <nonstd/span.hpp>

#include <cstddef>
#include <cstring>
#include <string_view>

namespace core {

class CacheEntryDataWriter
{
public:
  CacheEntryDataWriter(util::Bytes& output);

  // Write `data`. Throws `core::Error` on failure.
  void write_bytes(nonstd::span<const uint8_t> data);

  // Write `data`. Throws `core::Error` on failure.
  void write_str(std::string_view data);

  // Write integer `value`. Throws `core::Error` on failure.
  template<typename T> void write_int(T value);

private:
  util::Bytes& m_output;
};

inline CacheEntryDataWriter::CacheEntryDataWriter(util::Bytes& output)
  : m_output(output)
{
}

inline void
CacheEntryDataWriter::write_bytes(nonstd::span<const uint8_t> data)
{
  m_output.insert(m_output.end(), data);
}

template<typename T>
inline void
CacheEntryDataWriter::write_int(const T value)
{
  uint8_t buffer[sizeof(T)];
  util::int_to_big_endian(value, buffer);
  write_bytes(buffer);
}

inline void
CacheEntryDataWriter::write_str(std::string_view value)
{
  write_bytes(util::to_span(value));
}

} // namespace core
