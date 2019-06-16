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
#include "common_header.h"

bool common_header_from_file(FILE *f, struct common_header *header)
{
	char buffer[COMMON_HEADER_SIZE];
	if (fread(buffer, 1, sizeof(buffer), f) != sizeof(buffer)) {
		return false;
	}
	memcpy(header->magic, buffer, 4);
	header->version = buffer[4];
	header->compression_type = buffer[5];
	header->compression_level = buffer[6];
	header->content_size = UINT64_FROM_BYTES(buffer + 7);
	return true;
}

bool common_header_to_file(FILE *f, const struct common_header *header)
{
	char buffer[COMMON_HEADER_SIZE];
	memcpy(buffer, header->magic, 4);
	buffer[4] = header->version;
	buffer[5] = header->compression_type;
	buffer[6] = header->compression_level;
	BYTES_FROM_UINT64(buffer + 7, header->content_size);
	return fwrite(buffer, 1, sizeof(buffer), f) == sizeof(buffer);
}
