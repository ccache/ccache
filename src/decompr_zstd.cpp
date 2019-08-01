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

#include "ccache.hpp"
#include "compression.hpp"

#include <zstd.h>

enum stream_state {
  STREAM_STATE_READING,
  STREAM_STATE_FAILED,
  STREAM_STATE_END
};

struct state
{
  FILE* input;
  XXH64_state_t* checksum;
  char input_buffer[READ_BUFFER_SIZE];
  size_t input_size;
  size_t input_consumed;
  ZSTD_DStream* stream;
  ZSTD_inBuffer in;
  ZSTD_outBuffer out;
  enum stream_state stream_state;
};

static struct decompr_state*
decompr_zstd_init(FILE* input, XXH64_state_t* checksum)
{
  auto state = static_cast<struct state*>(malloc(sizeof(struct state)));

  state->input = input;
  state->checksum = checksum;
  state->input_size = 0;
  state->input_consumed = 0;
  state->stream = ZSTD_createDStream();
  state->stream_state = STREAM_STATE_READING;

  size_t ret = ZSTD_initDStream(state->stream);
  if (ZSTD_isError(ret)) {
    ZSTD_freeDStream(state->stream);
    free(state);
    return NULL;
  }
  return (struct decompr_state*)state;
}

static bool
decompr_zstd_read(struct decompr_state* handle, void* data, size_t size)
{
  if (!handle) {
    return false;
  }
  struct state* state = (struct state*)handle;

  size_t bytes_read = 0;
  while (bytes_read < size) {
    assert(state->input_size >= state->input_consumed);
    if (state->input_size == state->input_consumed) {
      state->input_size = fread(
        state->input_buffer, 1, sizeof(state->input_buffer), state->input);
      if (state->input_size == 0) {
        state->stream_state = STREAM_STATE_FAILED;
        return false;
      }
      state->input_consumed = 0;
    }

    state->in.src = (state->input_buffer + state->input_consumed);
    state->in.size = state->input_size - state->input_consumed;
    state->in.pos = 0;

    state->out.dst = ((char*)data + bytes_read);
    state->out.size = size - bytes_read;
    state->out.pos = 0;
    size_t ret = ZSTD_decompressStream(state->stream, &state->out, &state->in);
    if (ZSTD_isError(ret)) {
      state->stream_state = STREAM_STATE_FAILED;
      return false;
    }
    if (state->checksum) {
      XXH64_update(state->checksum, state->out.dst, state->out.pos);
    }
    if (ret == 0) {
      state->stream_state = STREAM_STATE_END;
      break;
    }
    bytes_read += state->out.pos;
    state->input_consumed += state->in.pos;
  }

  return true;
}

static bool
decompr_zstd_free(struct decompr_state* handle)
{
  if (!handle) {
    return false;
  }
  struct state* state = (struct state*)handle;
  ZSTD_freeDStream(state->stream);
  bool success = state->stream_state == STREAM_STATE_END;
  free(handle);
  return success;
}

struct decompressor decompressor_zstd_impl = {
  decompr_zstd_init, decompr_zstd_read, decompr_zstd_free};
