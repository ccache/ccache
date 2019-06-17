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
#include "conf.h"

extern struct conf *conf;

int8_t compression_level_from_config(void)
{
	return conf->compression ? conf->compression_level : 0;
}

enum compression_type compression_type_from_config(void)
{
	return conf->compression ? COMPR_TYPE_ZLIB : COMPR_TYPE_NONE;
}

const char *compression_type_to_string(uint8_t type)
{
	switch (type) {
	case COMPR_TYPE_NONE:
		return "none";

	case COMPR_TYPE_ZLIB:
		return "zlib";
	}

	return "unknown";
}

struct compressor *compressor_from_type(uint8_t type)
{
	switch (type) {
	case COMPR_TYPE_NONE:
		return &compressor_none_impl;

	case COMPR_TYPE_ZLIB:
		return &compressor_zlib_impl;
	}

	return NULL;
}

struct decompressor *decompressor_from_type(uint8_t type)
{
	switch (type) {
	case COMPR_TYPE_NONE:
		return &decompressor_none_impl;

	case COMPR_TYPE_ZLIB:
		return &decompressor_zlib_impl;

	}

	return NULL;
}


