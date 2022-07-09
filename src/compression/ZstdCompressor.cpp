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

ZstdCompressor::ZstdCompressor(core::Writer& writer,
                               int8_t compression_level,
                               std::string dict_dir,
                               int8_t entry_type)
  : m_writer(writer),
    m_zstd_stream(ZSTD_createCStream()),
    m_zstd_dict_id(0),
    m_zstd_dict(nullptr),
    m_zstd_in(std::make_unique<ZSTD_inBuffer_s>()),
    m_zstd_out(std::make_unique<ZSTD_outBuffer_s>())
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

#if ZSTD_VERSION_NUMBER >= 10400
  if (!dict_dir.empty() && entry_type != -1) {
    std::string dict_path = dict_path_from_entry_type(dict_dir, entry_type);
    auto st = Stat::stat(dict_path);
    if (st) {
      LOG("Using zstd compression dictionary from {}", dict_path);
      auto dict = Util::read_file(dict_path);
      m_zstd_dict =
        ZSTD_createCDict(dict.data(), dict.length(), m_compression_level);
#  if ZSTD_VERSION_NUMBER >= 10500
      m_zstd_dict_id = ZSTD_getDictID_fromCDict(m_zstd_dict);
#  else
      m_zstd_dict_id = ZSTD_getDictID_fromDict(dict.data(), dict.length());
#  endif
      LOG("Dictionary ID: {}", m_zstd_dict_id);
    }
    ZSTD_CCtx_refCDict(m_zstd_stream, m_zstd_dict);
  }
#endif
}

ZstdCompressor::~ZstdCompressor()
{
#if ZSTD_VERSION_NUMBER >= 10400
  ZSTD_freeCDict(m_zstd_dict);
#endif
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
  m_zstd_in->src = data;
  m_zstd_in->size = count;
  m_zstd_in->pos = 0;

  int flush = data ? 0 : 1;

  size_t ret;
  while (m_zstd_in->pos < m_zstd_in->size) {
    uint8_t buffer[CCACHE_READ_BUFFER_SIZE];
    m_zstd_out->dst = buffer;
    m_zstd_out->size = sizeof(buffer);
    m_zstd_out->pos = 0;
    ret = ZSTD_compressStream(m_zstd_stream, m_zstd_out.get(), m_zstd_in.get());
    ASSERT(!(ZSTD_isError(ret)));
    const size_t compressed_bytes = m_zstd_out->pos;
    if (compressed_bytes > 0) {
      m_writer.write(buffer, compressed_bytes);
    }
  }
  ret = flush;
  while (ret > 0) {
    uint8_t buffer[CCACHE_READ_BUFFER_SIZE];
    m_zstd_out->dst = buffer;
    m_zstd_out->size = sizeof(buffer);
    m_zstd_out->pos = 0;
    ret = ZSTD_endStream(m_zstd_stream, m_zstd_out.get());
    const size_t compressed_bytes = m_zstd_out->pos;
    if (compressed_bytes > 0) {
      m_writer.write(buffer, compressed_bytes);
    }
  }
}

void
ZstdCompressor::finalize()
{
  write(nullptr, 0);
  m_writer.finalize();
}

} // namespace compression
