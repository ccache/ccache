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

struct state
{
	FILE *input;
	char input_buffer[READ_BUFFER_SIZE];
	size_t input_size;
	size_t input_consumed;
	z_stream stream;
};

static struct decompr_state *
decompr_zlib_init(FILE *input)
{
	struct state *state = malloc(sizeof(struct state));

	state->input = input;
	state->input_size = 0;
	state->input_consumed = 0;
	state->stream.zalloc = Z_NULL;
	state->stream.zfree = Z_NULL;
	state->stream.opaque = Z_NULL;
	state->stream.avail_in = 0;
	state->stream.next_in = Z_NULL;

	int ret = inflateInit(&state->stream);
	if (ret != Z_OK) {
		free(state);
		return NULL;
	}
	return (struct decompr_state *)state;
}

static bool
decompr_zlib_read(struct decompr_state *handle, void *data, size_t size)
{
	struct state *state = (struct state *)handle;

	size_t bytes_read = 0;
	while (bytes_read < size) {
		assert(state->input_size >= state->input_consumed);
		if (state->input_size == state->input_consumed) {
			state->input_size = fread(
				state->input_buffer, 1, sizeof(state->input_buffer), state->input);
			if (state->input_size == 0) {
				return false;
			}
			state->input_consumed = 0;
		}

		state->stream.next_in =
			(Bytef *)(state->input_buffer + state->input_consumed);
		state->stream.avail_in = state->input_size - state->input_consumed;

		state->stream.next_out = (Bytef *)((char *)data + bytes_read);
		state->stream.avail_out = size - bytes_read;
		int ret = inflate(&state->stream, Z_NO_FLUSH);
		assert(ret != Z_STREAM_ERROR);
		switch (ret) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			return false;
		}
		bytes_read = size - state->stream.avail_out;
		state->input_consumed = state->input_size - state->stream.avail_in;
	}

	return true;
}

static void decompr_zlib_free(struct decompr_state *handle)
{
	struct state *state = (struct state *)handle;
	inflateEnd(&state->stream);
	free(handle);
}

struct decompressor decompr_zlib = {
	decompr_zlib_init,
	decompr_zlib_read,
	decompr_zlib_free
};
