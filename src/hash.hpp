// Copyright (C) 2018-2019 Joel Rosdahl and other contributors
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

#include "system.hpp"

#include "third_party/nonstd/string_view.hpp"

#define DIGEST_SIZE 20
#define DIGEST_STRING_BUFFER_SIZE (2 * DIGEST_SIZE + 1)

// struct digest represents the binary form of the final digest (AKA hash or
// checksum) produced by the hash algorithm.
struct digest
{
  uint8_t bytes[DIGEST_SIZE];
};

// Format the digest as a NUL-terminated hex string. The string buffer must
// contain at least DIGEST_STRING_BUFFER_SIZE bytes.
void digest_as_string(const struct digest* d, char* buffer);

// Return true if d1 and d2 are equal, else false.
bool digests_equal(const struct digest* d1, const struct digest* d2);

// struct hash represents the hash algorithm's inner state.
struct hash;

// Create a new hash state.
struct hash* hash_init();

// Create a new hash state from an existing hash state.
struct hash* hash_copy(struct hash* hash);

// Free a hash state created by hash_init or hash_copy.
void hash_free(struct hash* hash);

// Enable debug logging of hashed input to a binary and a text file.
void hash_enable_debug(struct hash* hash,
                       const char* section_name,
                       FILE* debug_binary,
                       FILE* debug_text);

// Retrieve the digest as bytes.
void hash_result_as_bytes(struct hash* hash, struct digest* digest);

// Retrieve the digest as a NUL-terminated hex string. The string buffer must
// contain at least DIGEST_STRING_BUFFER_SIZE bytes.
void hash_result_as_string(struct hash* hash, char* buffer);

// Hash some data that is unlikely to occur in the input. The idea is twofold:
//
// - Delimit things like arguments from each other (e.g., so that -I -O2 and
//   -I-O2 hash differently).
// - Tag different types of hashed information so that it's possible to do
//   conditional hashing of information in a safe way (e.g., if we want to hash
//   information X if CCACHE_A is set and information Y if CCACHE_B is set,
//   there should never be a hash collision risk).
void hash_delimiter(struct hash* hash, const char* type);

// Hash bytes in a buffer.
//
// If hash debugging is enabled, the bytes are written verbatim to the text
// input file.
void hash_buffer(struct hash* hash, const void* s, size_t len);

// Hash a NUL terminated string.
//
// If hash debugging is enabled, the string is written to the text input file
// followed by a newline.
void hash_string(struct hash* hash, const char* s);

// Hash a string with a known size.
//
// If hash debugging is enabled, the string is written to the text input file
// followed by a newline.
void hash_string_buffer(struct hash* hash, const char* s, size_t length);
void hash_string(struct hash* hash, const std::string& s);
void hash_string_view(struct hash* hash, nonstd::string_view sv);

// Hash an integer.
//
// If hash debugging is enabled, the integer is written in text form to the
// text input file followed by a newline.
void hash_int(struct hash* hash, int x);

// Add contents of an open file to the hash.
//
// If hash debugging is enabled, the data is written verbatim to the text input
// file.
//
// Returns true on success, otherwise false.
bool hash_fd(struct hash* hash, int fd, bool fd_is_file = false);

// Add contents of a file to the hash.
//
// If hash debugging is enabled, the data is written verbatim to the text input
// file.
//
// Returns true on success, otherwise false.
bool hash_file(struct hash* hash, const char* fname);
