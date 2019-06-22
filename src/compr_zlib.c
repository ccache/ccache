// Copyright (C) 2019 Joel Rosdahl
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

#include "ccache.h"
#include "compression.h"

#include <zlib.h>

struct state {
	FILE *output;
	z_stream stream;
	bool failed;
};

static struct compr_state *
compr_zlib_init(FILE *output, int level)
{
	struct state *state = malloc(sizeof(struct state));
	state->output = output;
	state->stream.zalloc = Z_NULL;
	state->stream.zfree = Z_NULL;
	state->stream.opaque = Z_NULL;
	state->failed = false;

	int ret = deflateInit(&state->stream, level);
	if (ret != Z_OK) {
		free(state);
		return NULL;
	}
	return (struct compr_state *)state;
}

static bool
compr_zlib_write(struct compr_state *handle, const void *data, size_t size)
{
	if (!handle) {
		return false;
	}
	struct state *state = (struct state *)handle;

	state->stream.next_in = (const Bytef *)data;
	state->stream.avail_in = size;

	int flush = data ? Z_NO_FLUSH : Z_FINISH;

	int ret;
	do {
		unsigned char buffer[READ_BUFFER_SIZE];
		state->stream.next_out = buffer;
		state->stream.avail_out = sizeof(buffer);
		ret = deflate(&state->stream, flush);
		assert(ret != Z_STREAM_ERROR);
		unsigned int compressed_bytes = sizeof(buffer) - state->stream.avail_out;
		if (fwrite(buffer, 1, compressed_bytes, state->output) != compressed_bytes
		    || ferror(state->output)) {
			state->failed = true;
			return false;
		}
	} while (state->stream.avail_out == 0);
	assert(state->stream.avail_in == 0);
	assert(ret == (flush ? Z_STREAM_END : Z_OK));

	return true;
}

static bool
compr_zlib_free(struct compr_state *handle)
{
	if (!handle) {
		return false;
	}
	struct state *state = (struct state *)handle;

	compr_zlib_write(handle, NULL, 0);
	deflateEnd(&state->stream);
	bool success = !state->failed;
	free(state);
	return success;
}

struct compressor compressor_zlib_impl = {
	compr_zlib_init,
	compr_zlib_write,
	compr_zlib_free
};
