// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#pragma once

#include <Util.hpp>
#include <core/exceptions.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

class Reader
{
public:
  virtual ~Reader() = default;

  // Read `count` bytes into `data`, returning the actual number of bytes read
  // if not enough data is available. Throws `core::Error` on failure, e.g. if
  // no bytes could be read.
  virtual size_t read(void* data, size_t count) = 0;

  // Read an integer. Throws Error on failure.
  template<typename T> T read_int();

  // Read an integer into `value`. Throws Error on failure.
  template<typename T> void read_int(T& value);

  // Read a string of length `length`. Throws `core::Error` on failure.
  std::string read_str(size_t length);
};

template<typename T>
inline T
Reader::read_int()
{
  uint8_t buffer[sizeof(T)];
  const auto bytes_read = read(buffer, sizeof(T));
  if (bytes_read != sizeof(T)) {
    throw core::Error("Read underflow");
  }
  T value;
  Util::big_endian_to_int(buffer, value);
  return value;
}

template<typename T>
inline void
Reader::read_int(T& value)
{
  value = read_int<T>();
}

inline std::string
Reader::read_str(const size_t length)
{
  std::string value(length, 0);
  read(&value[0], length);
  return value;
}

} // namespace core
