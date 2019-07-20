// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#ifndef COMPRESSION_H
#define COMPRESSION_H

#include "system.h"
#include "third_party/xxhash.h"

struct compr_state;

struct compressor {
	// Create and initialize a compressor.
	//
	// output: The file to write compressed data to.
	// compression_level: Desired compression level.
	// checksum: Checksum state to update (NULL for no checksum).
	struct compr_state *(*init)(
		FILE *output,
		int8_t compression_level,
		XXH64_state_t *checksum);

	// Get the actual compression level that will be used.
	int8_t (*get_actual_compression_level)(struct compr_state *state);

	// Compress data.
	//
	// data: Buffer to read decompressed data from.
	// size: How many bytes to read.
	//
	// Returns false on failure, otherwise true.
	bool (*write)(struct compr_state *state, const void *data, size_t size);

	// Finalize compressor and free its state.
	//
	// Returns false if finalization failed or if any previous operation failed,
	// otherwise true.
	bool (*free)(struct compr_state *state);
};

struct decompr_state;

struct decompressor {
	// Create and initialize a decompressor.
	//
	// input: The file to read compressed data from.
	// checksum: Checksum state to update (NULL for no checksum).
	struct decompr_state *(*init)(FILE *input, XXH64_state_t *checksum);

	// Decompress data.
	//
	// data: Buffer to write decompressed data to.
	// size: How many bytes to write.
	//
	// Returns false on failure, otherwise true.
	bool (*read)(struct decompr_state *state, void *data, size_t size);

	// Finalize the decompressor and free its state.
	//
	// Returns false if finalization failed or if any previous operation failed,
	// otherwise true.
	bool (*free)(struct decompr_state *state);
};

enum compression_type {
	COMPR_TYPE_NONE = 0,
	COMPR_TYPE_ZSTD = 1
};

extern struct compressor compressor_none_impl;
extern struct decompressor decompressor_none_impl;

extern struct compressor compressor_zstd_impl;
extern struct decompressor decompressor_zstd_impl;

int8_t compression_level_from_config(void);
enum compression_type compression_type_from_config(void);
const char *compression_type_to_string(uint8_t type);
struct compressor *compressor_from_type(uint8_t type);
struct decompressor *decompressor_from_type(uint8_t type);

#endif
