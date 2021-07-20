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

#include "ZstdCompressor.hpp"

#include "Logging.hpp"
#include "assertions.hpp"

#include <core/exceptions.hpp>

#include <algorithm>

namespace compression {

ZstdCompressor::ZstdCompressor(FILE* const stream, int8_t compression_level)
  : m_stream(stream),
    m_zstd_stream(ZSTD_createCStream())
{
  if (compression_level == 0) {
    compression_level = default_compression_level;
    LOG("Using default compression level {}", compression_level);
  }

  // libzstd 1.3.4 and newer support negative levels. However, the query
  // function ZSTD_minCLevel did not appear until 1.3.6, so perform detection
  // based on version instead.
  if (ZSTD_versionNumber() < 10304 && compression_level < 1) {
    LOG(
      "Using compression level 1 (minimum level supported by libzstd) instead"
      " of {}",
      compression_level);
    compression_level = 1;
  }

  m_compression_level = std::min<int>(compression_level, ZSTD_maxCLevel());
  if (m_compression_level != compression_level) {
    LOG("Using compression level {} (max libzstd level) instead of {}",
        m_compression_level,
        compression_level);
  }

  size_t ret = ZSTD_initCStream(m_zstd_stream, m_compression_level);
  if (ZSTD_isError(ret)) {
    ZSTD_freeCStream(m_zstd_stream);
    throw core::Error("error initializing zstd compression stream");
  }
}

ZstdCompressor::~ZstdCompressor()
{
  ZSTD_freeCStream(m_zstd_stream);
}

int8_t
ZstdCompressor::actual_compression_level() const
{
  return m_compression_level;
}

void
ZstdCompressor::write(const void* const data, const size_t count)
{
  m_zstd_in.src = data;
  m_zstd_in.size = count;
  m_zstd_in.pos = 0;

  int flush = data ? 0 : 1;

  size_t ret;
  while (m_zstd_in.pos < m_zstd_in.size) {
    uint8_t buffer[CCACHE_READ_BUFFER_SIZE];
    m_zstd_out.dst = buffer;
    m_zstd_out.size = sizeof(buffer);
    m_zstd_out.pos = 0;
    ret = ZSTD_compressStream(m_zstd_stream, &m_zstd_out, &m_zstd_in);
    ASSERT(!(ZSTD_isError(ret)));
    const size_t compressed_bytes = m_zstd_out.pos;
    if (fwrite(buffer, 1, compressed_bytes, m_stream) != compressed_bytes
        || ferror(m_stream)) {
      throw core::Error("failed to write to zstd output stream ");
    }
  }
  ret = flush;
  while (ret > 0) {
    uint8_t buffer[CCACHE_READ_BUFFER_SIZE];
    m_zstd_out.dst = buffer;
    m_zstd_out.size = sizeof(buffer);
    m_zstd_out.pos = 0;
    ret = ZSTD_endStream(m_zstd_stream, &m_zstd_out);
    const size_t compressed_bytes = m_zstd_out.pos;
    if (fwrite(buffer, 1, compressed_bytes, m_stream) != compressed_bytes
        || ferror(m_stream)) {
      throw core::Error("failed to write to zstd output stream");
    }
  }
}

void
ZstdCompressor::finalize()
{
  write(nullptr, 0);
}

} // namespace compression
