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

#include "compression.hpp"

struct state {
	FILE *input;
	XXH64_state_t *checksum;
	bool failed;
};

static struct decompr_state *
decompr_none_init(FILE *input, XXH64_state_t *checksum)
{
	auto state = static_cast<struct state*>(malloc(sizeof(struct state)));
	state->input = input;
	state->checksum = checksum;
	state->failed = false;
	return (struct decompr_state *)state;
}

static bool
decompr_none_read(struct decompr_state *handle, void *data, size_t size)
{
	struct state *state = (struct state *)handle;

	bool result = fread(data, 1, size, state->input) == size;
	if (result && state->checksum) {
		XXH64_update(state->checksum, data, size);
	}
	if (!result) {
		state->failed = true;
	}
	return result;
}

static bool
decompr_none_free(struct decompr_state *handle)
{
	struct state *state = (struct state *)handle;
	bool result = !state->failed;
	free(state);
	return result;
}

struct decompressor decompressor_none_impl = {
	decompr_none_init,
	decompr_none_read,
	decompr_none_free
};
