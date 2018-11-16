// Copyright (C) 2018 Joel Rosdahl
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

#ifndef HASH_H
#define HASH_H

#include "system.h"

struct hash;

// Create a new hash.
struct hash *hash_init(void);

// Create a new hash from an existing hash state.
struct hash *hash_copy(struct hash *hash);

// Free a hash created by hash_init or hash_copy.
void hash_free(struct hash *hash);

// Enable debug logging of hashed input to a binary and a text file.
void hash_enable_debug(
	struct hash *hash, const char *section_name, FILE *debug_binary,
	FILE *debug_text);

// Return how many bytes have been hashed.
size_t hash_input_size(struct hash *hash);

// Return the hash result as a hex string. Caller frees.
char *hash_result(struct hash *hash);

// Return the hash result as 16 binary bytes.
void hash_result_as_bytes(struct hash *hash, unsigned char *out);

// Return whether hash1 and hash2 are equal.
bool hash_equal(struct hash *hash1, struct hash *hash2);

// Hash some data that is unlikely to occur in the input. The idea is twofold:
//
// - Delimit things like arguments from each other (e.g., so that -I -O2 and
//   -I-O2 hash differently).
// - Tag different types of hashed information so that it's possible to do
//   conditional hashing of information in a safe way (e.g., if we want to hash
//   information X if CCACHE_A is set and information Y if CCACHE_B is set,
//   there should never be a hash collision risk).
void hash_delimiter(struct hash *hash, const char *type);

// Hash bytes in a buffer.
//
// If hash debugging is enabled, the bytes are written verbatim to the text
// input file.
void hash_buffer(struct hash *hash, const void *s, size_t len);

// Hash a string.
//
// If hash debugging is enabled, the string is written to the text input file
// followed by a newline.
void hash_string(struct hash *hash, const char *s);

// Hash a string with a known size.
//
// If hash debugging is enabled, the string is written to the text input file
// followed by a newline.
void hash_string_buffer(struct hash *hash, const char *s, int length);

// Hash an integer.
//
// If hash debugging is enabled, the integer is written in text form to the
// text input file followed by a newline.
void hash_int(struct hash *hash, int x);

// Add contents of an open file to the hash.
//
// If hash debugging is enabled, the data is written verbatim to the text input
// file.
//
// Returns true on success, otherwise false.
bool hash_fd(struct hash *hash, int fd);

// Add contents of a file to the hash.
//
// If hash debugging is enabled, the data is written verbatim to the text input
// file.
//
// Returns true on success, otherwise false.
bool hash_file(struct hash *hash, const char *fname);

#endif
