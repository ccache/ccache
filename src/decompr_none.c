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

#include "compression.h"

static struct decompr_state *
decompr_none_init(FILE *input)
{
	return (struct decompr_state *)input;
}

static bool
decompr_none_read(struct decompr_state *handle, void *data, size_t size)
{
	FILE *input = (FILE *)handle;
	return fread(data, 1, size, input) == size;
}

static bool
decompr_none_free(struct decompr_state *handle)
{
	FILE *input = (FILE *)handle;
	return ferror(input) == 0;
}

struct decompressor decompressor_none_impl = {
	decompr_none_init,
	decompr_none_read,
	decompr_none_free
};
