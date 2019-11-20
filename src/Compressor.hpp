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

#pragma once

#include "system.hpp"

#include "Compression.hpp"

#include <memory>

class Compressor
{
public:
  virtual ~Compressor() = default;

  // Create a compressor for the specified type.
  //
  // Parameters:
  // - type: The type.
  // - stream: The stream to write to.
  // - compression_level: Desired compression level.
  static std::unique_ptr<Compressor> create_from_type(Compression::Type type,
                                                      FILE* stream,
                                                      int8_t compression_level);

  // Get the actual compression level used for the compressed stream.
  virtual int8_t actual_compression_level() const = 0;

  // Write data from a buffer to the compressed stream.
  //
  // Parameters:
  // - data: Data to write.
  // - count: Size of data to write.
  //
  // Throws Error on failure.
  virtual void write(const void* data, size_t count) = 0;

  // Write an unsigned integer to the compressed stream.
  //
  // Parameters:
  // - value: Value to write.
  //
  // Throws Error on failure.
  template<typename T> void write(T value);

  // Finalize compression.
  //
  // This method checks that the end state of the compressed stream is correct
  // and throws Error if not.
  virtual void finalize() = 0;
};
