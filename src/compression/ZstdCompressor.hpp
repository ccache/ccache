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

#include "Compressor.hpp"

#include <NonCopyable.hpp>

#include <cstdint>
#include <memory>

struct ZSTD_CCtx_s;
struct ZSTD_inBuffer_s;
struct ZSTD_outBuffer_s;

namespace compression {

// A compressor of a Zstandard stream.
class ZstdCompressor : public Compressor, NonCopyable
{
public:
  // Parameters:
  // - stream: The file to write data to.
  // - compression_level: Desired compression level.
  ZstdCompressor(FILE* stream, int8_t compression_level);

  ~ZstdCompressor() override;

  int8_t actual_compression_level() const override;
  void write(const void* data, size_t count) override;
  void finalize() override;

  constexpr static uint8_t default_compression_level = 1;

private:
  FILE* m_stream;
  ZSTD_CCtx_s* m_zstd_stream;
  std::unique_ptr<ZSTD_inBuffer_s> m_zstd_in;
  std::unique_ptr<ZSTD_outBuffer_s> m_zstd_out;
  int8_t m_compression_level;
};

} // namespace compression
