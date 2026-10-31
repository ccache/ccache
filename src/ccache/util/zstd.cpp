// Copyright (C) 2022-2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "zstd.hpp"

#include <zstd.h>

namespace util {

ZstdCompressor::ZstdCompressor(Bytes& output)
  : m_context(ZSTD_createCCtx()),
    m_output(output)
{
}

ZstdCompressor::~ZstdCompressor()
{
  ZSTD_freeCCtx(m_context);
}

tl::expected<void, std::string>
ZstdCompressor::initialize(size_t input_size, int8_t compression_level)
{
  if (!m_context) {
    return tl::unexpected("Failed to create Zstd compression context");
  }
  const size_t result = ZSTD_initCStream(m_context, compression_level);
  if (ZSTD_isError(result)) {
    return tl::unexpected(ZSTD_getErrorName(result));
  }
  m_remaining_input = input_size;
  m_output_limit = m_output.size() + zstd_compress_bound(input_size);
  m_output.reserve(m_output_limit);
  return {};
}

tl::expected<void, std::string>
ZstdCompressor::write(std::span<const uint8_t> input)
{
  if (input.size() > m_remaining_input) {
    return tl::unexpected("Input exceeds expected size");
  }
  m_remaining_input -= input.size();
  return compress(input, false);
}

tl::expected<void, std::string>
ZstdCompressor::finish()
{
  if (m_remaining_input != 0) {
    return tl::unexpected("Input is smaller than expected size");
  }
  return compress({}, true);
}

tl::expected<void, std::string>
ZstdCompressor::compress(std::span<const uint8_t> input, bool finish)
{
  ZSTD_inBuffer in{.src = input.data(), .size = input.size(), .pos = 0};
  size_t remaining;
  do {
    const size_t offset = m_output.size();
    m_output.resize(m_output_limit);
    ZSTD_outBuffer out{
      .dst = m_output.data(), .size = m_output_limit, .pos = offset};
    remaining = finish ? ZSTD_endStream(m_context, &out)
                       : ZSTD_compressStream(m_context, &out, &in);
    m_output.resize(out.pos);
    if (ZSTD_isError(remaining)) {
      return tl::unexpected(ZSTD_getErrorName(remaining));
    }
    if (out.pos == m_output_limit
        && (in.pos < in.size || (finish && remaining))) {
      return tl::unexpected("Compressed output exceeds compression bound");
    }
  } while (in.pos < in.size || (finish && remaining != 0));
  return {};
}

tl::expected<void, std::string>
zstd_compress(std::span<const uint8_t> input,
              Bytes& output,
              int8_t compression_level)
{
  const size_t original_output_size = output.size();
  const size_t compress_bound = zstd_compress_bound(input.size());
  output.resize(original_output_size + compress_bound);

  const size_t ret = ZSTD_compress(&output[original_output_size],
                                   compress_bound,
                                   input.data(),
                                   input.size(),
                                   compression_level);
  if (ZSTD_isError(ret)) {
    return tl::unexpected(ZSTD_getErrorName(ret));
  }

  output.resize(original_output_size + ret);
  return {};
}

tl::expected<void, std::string>
zstd_decompress(std::span<const uint8_t> input,
                Bytes& output,
                size_t original_size)
{
  if (original_size == 0) {
    return {};
  }

  const size_t original_output_size = output.size();

  output.resize(original_output_size + original_size);
  const size_t ret = ZSTD_decompress(
    &output[original_output_size], original_size, input.data(), input.size());
  if (ZSTD_isError(ret)) {
    return tl::unexpected(ZSTD_getErrorName(ret));
  }

  output.resize(original_output_size + ret);

  return {};
}

size_t
zstd_compress_bound(size_t input_size)
{
  return ZSTD_compressBound(input_size);
}

std::tuple<int8_t, std::string>
zstd_supported_compression_level(int8_t wanted_level)
{
  const int8_t level =
    static_cast<int8_t>(std::min<int>(wanted_level, ZSTD_maxCLevel()));
  if (level != wanted_level) {
    return {level, "max libzstd level"};
  }

  return {level, {}};
}

} // namespace util
