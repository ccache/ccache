// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include <tl/expected.hpp>

#include "third_party/blake3/blake3.h"
#include <third_party/nonstd/span.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

// This class represents a hash state.
class Hash
{
public:
  using Digest = std::array<uint8_t, 20>;

  Hash();
  Hash(const Hash& other) = default;

  Hash& operator=(const Hash& other) = default;

  // Enable debug logging of the hashed input to a binary and a text file.
  void enable_debug(std::string_view section_name,
                    FILE* debug_binary,
                    FILE* debug_text);

  // Retrieve the digest.
  Digest digest() const;

  // Hash some data that is unlikely to occur in the input. The idea is twofold:
  //
  // - Delimit things like arguments from each other (e.g., so that -I -O2 and
  //   -I-O2 hash differently).
  // - Tag different types of hashed information so that it's possible to do
  //   conditional hashing of information in a safe way (e.g., if we want to
  //   hash information X if CCACHE_A is set and information Y if CCACHE_B is
  //   set, there should never be a hash collision risk).
  Hash& hash_delimiter(std::string_view type);

  // Add data to the hash.
  //
  // If hash debugging is enabled the bytes will be written verbatim to the text
  // input file, plus a final newline character.
  Hash& hash(nonstd::span<const uint8_t> data);
  Hash& hash(const char* data, size_t size);
  Hash& hash(std::string_view data);

  // Add an integer to the hash.
  //
  // If hash debugging is enabled, the integer is written in text form to the
  // text input file followed by a newline.
  Hash& hash(int64_t x);

  // Add file contents to the hash.
  //
  // If hash debugging is enabled, the data is written verbatim to the text
  // input file.
  tl::expected<void, std::string> hash_file(const std::string& path);

  // Add contents read from an open file descriptor to the hash.
  //
  // If hash debugging is enabled, the data is written verbatim to the text
  // input file.
  tl::expected<void, std::string> hash_fd(int fd);

private:
  blake3_hasher m_hasher;
  FILE* m_debug_binary = nullptr;
  FILE* m_debug_text = nullptr;

  void hash_buffer(nonstd::span<const uint8_t> buffer);
  void hash_buffer(std::string_view buffer);

  void add_debug_text(nonstd::span<const uint8_t> text);
  void add_debug_text(std::string_view text);
};
