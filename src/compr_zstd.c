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

#ifdef HAVE_ZSTD_H
#include <zstd.h>
#endif

#ifdef HAVE_LIBZSTD

struct state {
	FILE *output;
	ZSTD_CStream *stream;
	ZSTD_inBuffer in;
	ZSTD_outBuffer out;
	bool failed;
};

static struct compr_state *
compr_zstd_init(FILE *output, int8_t level)
{
	struct state *state = malloc(sizeof(struct state));
	state->output = output;
	state->stream = ZSTD_createCStream();
	state->failed = false;

	size_t ret = ZSTD_initCStream(state->stream, level);
	if (ZSTD_isError(ret)) {
		ZSTD_freeCStream(state->stream);
		free(state);
		return NULL;
	}
	return (struct compr_state *)state;
}

static bool
compr_zstd_write(struct compr_state *handle, const void *data, size_t size)
{
	if (!handle) {
		return false;
	}
	struct state *state = (struct state *)handle;

	state->in.src = data;
	state->in.size = size;
	state->in.pos = 0;

	int flush = data ? 0 : 1;

	size_t ret;
	while (state->in.pos < state->in.size) {
		unsigned char buffer[READ_BUFFER_SIZE];
		state->out.dst = buffer;
		state->out.size = sizeof(buffer);
		state->out.pos = 0;
		ret = ZSTD_compressStream(state->stream, &state->out, &state->in);
		assert(!(ZSTD_isError(ret)));
		size_t compressed_bytes = state->out.pos;
		if (fwrite(buffer, 1, compressed_bytes, state->output) != compressed_bytes
		    || ferror(state->output)) {
			state->failed = true;
			return false;
		}
	}
	ret = flush;
	while (ret > 0) {
		unsigned char buffer[READ_BUFFER_SIZE];
		state->out.dst = buffer;
		state->out.size = sizeof(buffer);
		state->out.pos = 0;
		ret = ZSTD_endStream(state->stream, &state->out);
		size_t compressed_bytes = state->out.pos;
		if (fwrite(buffer, 1, compressed_bytes, state->output) != compressed_bytes
		    || ferror(state->output)) {
			state->failed = true;
			return false;
		}
	}

	return true;
}

static bool
compr_zstd_free(struct compr_state *handle)
{
	if (!handle) {
		return false;
	}
	struct state *state = (struct state *)handle;

	compr_zstd_write(handle, NULL, 0);
	ZSTD_freeCStream(state->stream);
	bool success = !state->failed;
	free(state);
	return success;
}

struct compressor compressor_zstd_impl = {
	compr_zstd_init,
	compr_zstd_write,
	compr_zstd_free
};

#endif // HAVE_LIBZSTD
