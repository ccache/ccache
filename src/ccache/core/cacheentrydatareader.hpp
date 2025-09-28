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
#include <string_view>

namespace core {

class CacheEntryDataReader
{
public:
  CacheEntryDataReader(nonstd::span<const uint8_t> data);

  // Read `size` bytes. Throws `core::Error` on failure.
  nonstd::span<const uint8_t> read_bytes(size_t size);

  // Read and copy `buffer.size()` bytes into `buffer`. Throws `core::Error` on
  // failure.
  void read_and_copy_bytes(nonstd::span<uint8_t> buffer);

  // Read a string of length `length`. Throws `core::Error` on failure.
  std::string_view read_str(size_t length);

  // Read an integer. Throws `core::Error` on failure.
  template<typename T> T read_int();

  // Read an integer into `value`. Throws `core::Error` on failure.
  template<typename T> void read_int(T& value);

private:
  nonstd::span<const uint8_t> m_data;
};

inline CacheEntryDataReader::CacheEntryDataReader(
  nonstd::span<const uint8_t> data)
  : m_data(data)
{
}

inline nonstd::span<const uint8_t>
CacheEntryDataReader::read_bytes(size_t size)
{
  if (size > m_data.size()) {
    throw core::Error(FMT("CacheEntryDataReader: data underflow of {} bytes",
                          size - m_data.size()));
  }
  const auto bytes = m_data.first(size);
  m_data = m_data.subspan(size);
  return bytes;
}

inline void
CacheEntryDataReader::read_and_copy_bytes(nonstd::span<uint8_t> buffer)
{
  const auto span = read_bytes(buffer.size());
  memcpy(buffer.data(), span.data(), span.size());
}

inline std::string_view
CacheEntryDataReader::read_str(const size_t length)
{
  return util::to_string_view(read_bytes(length));
}

template<typename T>
inline T
CacheEntryDataReader::read_int()
{
  const auto buffer = read_bytes(sizeof(T));
  T value;
  util::big_endian_to_int(buffer.data(), value);
  return value;
}

template<typename T>
inline void
CacheEntryDataReader::read_int(T& value)
{
  value = read_int<T>();
}

} // namespace core
