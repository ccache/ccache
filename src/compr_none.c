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

static struct compr_state *
compr_none_init(FILE *output, int8_t level)
{
	(void)level;
	return (struct compr_state *)output;
}

static int8_t
compr_none_get_actual_compression_level(struct compr_state *handle)
{
	(void)handle;
	return 0;
}

static bool
compr_none_write(struct compr_state *handle, const void *data, size_t size)
{
	FILE *output = (FILE *)handle;
	return fwrite(data, 1, size, output) == size;
}

static bool
compr_none_free(struct compr_state *handle)
{
	FILE *output = (FILE *)handle;
	return ferror(output) == 0;
}

struct compressor compressor_none_impl = {
	compr_none_init,
	compr_none_get_actual_compression_level,
	compr_none_write,
	compr_none_free
};
