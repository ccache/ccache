// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include <compression/types.hpp>

#include <cstdio>
#include <memory>

namespace compression {

class Decompressor
{
public:
  virtual ~Decompressor() = default;

  // Create a decompressor for the specified type.
  //
  // Parameters:
  // - type: The type.
  // - stream: The stream to read from.
  static std::unique_ptr<Decompressor> create_from_type(Type type,
                                                        FILE* stream);

  // Read data into a buffer from the compressed stream.
  //
  // Parameters:
  // - data: Buffer to write decompressed data to.
  // - count: How many bytes to write.
  //
  // Throws Error on failure.
  virtual void read(void* data, size_t count) = 0;

  // Finalize decompression.
  //
  // This method checks that the end state of the compressed stream is correct
  // and throws Error if not.
  virtual void finalize() = 0;
};

} // namespace compression
