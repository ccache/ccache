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
#include <assertions.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

class Writer
{
public:
  virtual ~Writer() = default;

  // Write `count` bytes from `data`. Throws `core::Error` on failure.
  virtual void write(const void* data, size_t count) = 0;

  // Write integer `value`. Throws `core::Error` on failure.
  template<typename T> void write_int(T value);

  // Write `value`. Throws `core::Error` on failure.
  void write_str(const std::string& value);

  // Finalize writing, e.g. flush written bytes and potentially check for error
  // states. Throws `core::Error` on failure.
  virtual void finalize() = 0;
};

template<typename T>
inline void
Writer::write_int(const T value)
{
  uint8_t buffer[sizeof(T)];
  Util::int_to_big_endian(value, buffer);
  write(buffer, sizeof(T));
}

inline void
Writer::write_str(const std::string& value)
{
  write(value.data(), value.length());
}

} // namespace core
