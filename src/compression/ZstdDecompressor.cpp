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

#include "ZstdDecompressor.hpp"

#include "Logging.hpp"
#include "assertions.hpp"

#include <core/exceptions.hpp>

namespace compression {

ZstdDecompressor::ZstdDecompressor(core::Reader& reader,
                                   std::string dict_dir,
                                   int8_t entry_type)
  : m_reader(reader),
    m_entry_type(entry_type),
    m_input_size(0),
    m_input_consumed(0),
    m_zstd_stream(ZSTD_createDStream()),
    m_zstd_dict_id(0),
    m_zstd_dict(nullptr),
    m_reached_stream_end(false)
{
  const size_t ret = ZSTD_initDStream(m_zstd_stream);
  if (ZSTD_isError(ret)) {
    ZSTD_freeDStream(m_zstd_stream);
    throw core::Error("failed to initialize zstd decompression stream");
  }

#if ZSTD_VERSION_NUMBER >= 10400
  if (!dict_dir.empty() && entry_type != -1) {
    std::string dict_path = dict_path_from_entry_type(dict_dir, entry_type);
    auto st = Stat::stat(dict_path);
    if (st) {
      LOG("Using zstd decompression dictionary from {}", dict_path);
      auto dict = Util::read_file(dict_path);
      m_zstd_dict = ZSTD_createDDict(dict.data(), dict.length());
      m_zstd_dict_id = ZSTD_getDictID_fromDDict(m_zstd_dict);
      LOG("Dictionary ID: {}", m_zstd_dict_id);
    }
  }
  ZSTD_DCtx_refDDict(m_zstd_stream, m_zstd_dict);
#endif
}

ZstdDecompressor::~ZstdDecompressor()
{
#if ZSTD_VERSION_NUMBER >= 10400
  ZSTD_freeDDict(m_zstd_dict);
#endif
  ZSTD_freeDStream(m_zstd_stream);
}

size_t
ZstdDecompressor::read(void* const data, const size_t count)
{
  size_t bytes_read = 0;
  while (bytes_read < count) {
    ASSERT(m_input_size >= m_input_consumed);
    if (m_input_size == m_input_consumed) {
      m_input_size = m_reader.read(m_input_buffer, sizeof(m_input_buffer));
      m_input_consumed = 0;
    }

    m_zstd_in.src = (m_input_buffer + m_input_consumed);
    m_zstd_in.size = m_input_size - m_input_consumed;
    m_zstd_in.pos = 0;

    m_zstd_out.dst = static_cast<uint8_t*>(data) + bytes_read;
    m_zstd_out.size = count - bytes_read;
    m_zstd_out.pos = 0;
    const size_t ret =
      ZSTD_decompressStream(m_zstd_stream, &m_zstd_out, &m_zstd_in);
    if (ZSTD_isError(ret)) {
      throw core::Error("Failed to read from zstd input stream");
    }
    if (ret == 0) {
      m_reached_stream_end = true;
      break;
    }
    bytes_read += m_zstd_out.pos;
    m_input_consumed += m_zstd_in.pos;
  }

  return count;
}

void
ZstdDecompressor::finalize()
{
  if (!m_reached_stream_end) {
    throw core::Error("Garbage data at end of zstd input stream");
  }
}

} // namespace compression
