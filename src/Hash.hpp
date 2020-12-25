// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "core/system.hpp"

#include "Digest.hpp"

#include "third_party/blake3/blake3.h"
#include "third_party/nonstd/string_view.hpp"

// This class represents a hash state.
class Hash
{
public:
  enum class HashType { binary, text };

  Hash();
  Hash(const Hash& other) = default;

  Hash& operator=(const Hash& other) = default;

  // Enable debug logging of the hashed input to a binary and a text file.
  void enable_debug(nonstd::string_view section_name,
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
  Hash& hash_delimiter(nonstd::string_view type);

  // Add bytes to the hash.
  //
  // If hash debugging is enabled:
  //
  // - If `hash_type` is `HashType::binary`, the buffer content is written in
  //   hex format to the text input file.
  // - If `hash_type` is `HashType::text`, the buffer content is written
  //   verbatim to the text input file.
  //
  // In both cases a newline character is added as well.
  Hash&
  hash(const void* data, size_t size, HashType hash_type = HashType::text);

  // Add a string to the hash.
  //
  // If hash debugging is enabled, the string is written to the text input file
  // followed by a newline.
  Hash& hash(nonstd::string_view data);

  // Add an integer to the hash.
  //
  // If hash debugging is enabled, the integer is written in text form to the
  // text input file followed by a newline.
  Hash& hash(int64_t x);

  // Add contents read from an open file descriptor to the hash.
  //
  // If hash debugging is enabled, the data is written verbatim to the text
  // input file.
  //
  // Returns true on success, otherwise false.
  bool hash_fd(int fd);

  // Add file contents to the hash.
  //
  // If hash debugging is enabled, the data is written verbatim to the text
  // input file.
  //
  // Returns true on success, otherwise false.
  bool hash_file(const std::string& path);

private:
  blake3_hasher m_hasher;
  FILE* m_debug_binary = nullptr;
  FILE* m_debug_text = nullptr;

  void hash_buffer(nonstd::string_view buffer);
  void add_debug_text(nonstd::string_view text);
};
