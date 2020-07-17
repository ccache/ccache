// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "exceptions.hpp"
#include "logging.hpp"

const uint8_t k_default_zstd_compression_level = 1;

ZstdCompressor::ZstdCompressor(FILE* stream, int8_t compression_level)
  : m_stream(stream), m_zstd_stream(ZSTD_createCStream())
{
  if (compression_level == 0) {
    compression_level = k_default_zstd_compression_level;
    cc_log("Using default compression level %d", compression_level);
  }

  // libzstd 1.3.4 and newer support negative levels. However, the query
  // function ZSTD_minCLevel did not appear until 1.3.6, so perform detection
  // based on version instead.
  if (ZSTD_versionNumber() < 10304 && compression_level < 1) {
    cc_log(
      "Using compression level 1 (minimum level supported by libzstd) instead"
      " of %d",
      compression_level);
    compression_level = 1;
  }

  m_compression_level = std::min<int>(compression_level, ZSTD_maxCLevel());
  if (m_compression_level != compression_level) {
    cc_log("Using compression level %d (max libzstd level) instead of %d",
           m_compression_level,
           compression_level);
  }

  size_t ret = ZSTD_initCStream(m_zstd_stream, m_compression_level);
  if (ZSTD_isError(ret)) {
    ZSTD_freeCStream(m_zstd_stream);
    throw Error("error initializing zstd compression stream");
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
ZstdCompressor::write(const void* data, size_t count)
{
  m_zstd_in.src = data;
  m_zstd_in.size = count;
  m_zstd_in.pos = 0;

  int flush = data ? 0 : 1;

  size_t ret;
  while (m_zstd_in.pos < m_zstd_in.size) {
    uint8_t buffer[READ_BUFFER_SIZE];
    m_zstd_out.dst = buffer;
    m_zstd_out.size = sizeof(buffer);
    m_zstd_out.pos = 0;
    ret = ZSTD_compressStream(m_zstd_stream, &m_zstd_out, &m_zstd_in);
    assert(!(ZSTD_isError(ret)));
    size_t compressed_bytes = m_zstd_out.pos;
    if (fwrite(buffer, 1, compressed_bytes, m_stream) != compressed_bytes
        || ferror(m_stream)) {
      throw Error("failed to write to zstd output stream ");
    }
  }
  ret = flush;
  while (ret > 0) {
    uint8_t buffer[READ_BUFFER_SIZE];
    m_zstd_out.dst = buffer;
    m_zstd_out.size = sizeof(buffer);
    m_zstd_out.pos = 0;
    ret = ZSTD_endStream(m_zstd_stream, &m_zstd_out);
    size_t compressed_bytes = m_zstd_out.pos;
    if (fwrite(buffer, 1, compressed_bytes, m_stream) != compressed_bytes
        || ferror(m_stream)) {
      throw Error("failed to write to zstd output stream");
    }
  }
}

void
ZstdCompressor::finalize()
{
  write(nullptr, 0);
}
