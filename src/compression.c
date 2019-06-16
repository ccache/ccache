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
	unsigned conf_compression_level;
#ifdef USE_ZSTD
	conf_compression_level = conf->compression_level;
#else
	conf_compression_level = conf->compression_level;
#endif
	return conf->compression ? conf_compression_level : 0;
}

enum compression_type compression_type_from_config(void)
{
	enum compression_type conf_compression_type;
#ifdef USE_ZSTD
	conf_compression_type = COMPR_TYPE_ZSTD;
#else
	conf_compression_type = COMPR_TYPE_ZLIB;
#endif
	return conf->compression ? conf_compression_type : COMPR_TYPE_NONE;
}

const char *compression_type_to_string(uint8_t type)
{
	switch (type) {
	case COMPR_TYPE_NONE:
		return "none";

	case COMPR_TYPE_ZLIB:
		return "zlib";

	case COMPR_TYPE_ZSTD:
		return "zstd";
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

	case COMPR_TYPE_ZSTD:
#ifdef HAVE_LIBZSTD
		return &compressor_zstd_impl;
#else
		return NULL;
#endif
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

	case COMPR_TYPE_ZSTD:
#ifdef HAVE_LIBZSTD
		return &decompressor_zstd_impl;
#else
		return NULL;
#endif
	}

	return NULL;
}


