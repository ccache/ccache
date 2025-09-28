// Copyright (C) 2023-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/bytes.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace util {

// --- Interface ---

// Get an integer value from bytes in big endian order.
//
// Parameters:
// - buffer: Bytes to read.
// - count: Number of bytes to read.
template<typename T> void big_endian_to_int(const uint8_t* buffer, T& value);

// Write bytes in big endian order from an integer value.
//
// Parameters:
// - value: Integer value to read.
// - buffer: Buffer to write bytes to.
template<typename T> void int_to_big_endian(T value, uint8_t* buffer);

// Convert `data` to a `nonstd::span<const uint8_t>`.
nonstd::span<const uint8_t> to_span(const void* data, size_t size);

// Convert `value` to a `nonstd::span<const uint8_t>`.
nonstd::span<const uint8_t> to_span(std::string_view value);

// Convert `value` to a string. This function is used when joining
// `std::string`s with `util::join`.
template<typename T> std::string to_string(const T& value);

// Convert `data` to a `std::string_view`.
std::string_view to_string_view(nonstd::span<const uint8_t> data);

// --- Inline implementations ---

template<typename T>
void
big_endian_to_int(const uint8_t* buffer, T& value)
{
  value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    value <<= 8;
    value |= buffer[i];
  }
}

template<>
inline void
big_endian_to_int(const uint8_t* buffer, int8_t& value)
{
  value = buffer[0];
}

template<>
inline void
big_endian_to_int(const uint8_t* buffer, uint8_t& value)
{
  value = buffer[0];
}

template<typename T>
void
int_to_big_endian(T value, uint8_t* buffer)
{
  for (size_t i = 0; i < sizeof(T); ++i) {
    buffer[sizeof(T) - i - 1] = static_cast<uint8_t>(value & 0xFF);
    value >>= 8;
  }
}

template<>
inline void
int_to_big_endian(uint8_t value, uint8_t* buffer)
{
  buffer[0] = value;
}

template<>
inline void
int_to_big_endian(int8_t value, uint8_t* buffer)
{
  buffer[0] = value;
}

inline nonstd::span<const uint8_t>
to_span(const void* data, size_t size)
{
  return {reinterpret_cast<const uint8_t*>(data), size};
}

inline nonstd::span<const uint8_t>
to_span(std::string_view data)
{
  return to_span(data.data(), data.size());
}

template<typename T>
inline std::string
to_string(const T& t)
{
  using std::to_string;
  return to_string(std::forward<T>(t));
}

template<>
inline std::string
to_string(const std::string& string)
{
  return std::string(string);
}

template<>
inline std::string
to_string(const std::string_view& sv)
{
  return std::string(sv);
}

template<>
inline std::string
to_string(const nonstd::span<const uint8_t>& bytes)
{
  return std::string(to_string_view(bytes));
}

template<>
inline std::string
to_string(const Bytes& bytes)
{
  return std::string(to_string_view(bytes));
}

template<>
inline std::string
to_string(const std::filesystem::path& path)
{
  return path.string();
}

inline std::string_view
to_string_view(nonstd::span<const uint8_t> data)
{
  return std::string_view(reinterpret_cast<const char*>(data.data()),
                          data.size());
}

} // namespace util
