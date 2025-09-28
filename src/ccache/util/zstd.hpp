// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/bytes.hpp>

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

namespace util {

[[nodiscard]] tl::expected<void, std::string> zstd_compress(
  nonstd::span<const uint8_t> input, Bytes& output, int8_t compression_level);

[[nodiscard]] tl::expected<void, std::string> zstd_decompress(
  nonstd::span<const uint8_t> input, Bytes& output, size_t original_size);

size_t zstd_compress_bound(size_t input_size);

std::tuple<int8_t, std::string>
zstd_supported_compression_level(int8_t wanted_level);

} // namespace util
