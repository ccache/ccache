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

#include "Decompressor.hpp"

#include <zstd.h>

#include <cstdint>

namespace compression {

// A decompressor of a Zstandard stream.
class ZstdDecompressor : public Decompressor
{
public:
  // Parameters:
  // - stream: The file to read data from.
  explicit ZstdDecompressor(FILE* stream);

  ~ZstdDecompressor() override;

  void read(void* data, size_t count) override;
  void finalize() override;

private:
  FILE* m_stream;
  char m_input_buffer[CCACHE_READ_BUFFER_SIZE];
  size_t m_input_size;
  size_t m_input_consumed;
  ZSTD_DStream* m_zstd_stream;
  ZSTD_inBuffer m_zstd_in;
  ZSTD_outBuffer m_zstd_out;
  bool m_reached_stream_end;
};

} // namespace compression
