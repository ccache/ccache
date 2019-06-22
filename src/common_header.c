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
#include "int_bytes_conversion.h"
#include "compression.h"
#include "common_header.h"

void common_header_init_from_config(
	struct common_header *header,
	const char magic[4],
	uint8_t RESULT_VERSION,
	uint64_t content_size)
{
	int8_t compr_level = compression_level_from_config();
	enum compression_type compr_type = compression_type_from_config();

	memcpy(header->magic, magic, 4);
	header->version = RESULT_VERSION;
	header->compression_type = compr_type;
	header->compression_level = compr_level;
	header->content_size = content_size;
}

bool common_header_init_from_file(struct common_header *header, FILE *f)
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

bool common_header_write_to_file(const struct common_header *header, FILE *f)
{
	char buffer[COMMON_HEADER_SIZE];
	memcpy(buffer, header->magic, 4);
	buffer[4] = header->version;
	buffer[5] = header->compression_type;
	buffer[6] = header->compression_level;
	BYTES_FROM_UINT64(buffer + 7, header->content_size);
	return fwrite(buffer, 1, sizeof(buffer), f) == sizeof(buffer);
}

bool common_header_verify(
	const struct common_header *header, int fd, const char *name, char **errmsg)
{
	if (header->compression_type == COMPR_TYPE_NONE) {
		// Since we have the size available, let's use it as a super primitive
		// consistency check for the non-compressed case. (A real checksum is used
		// for compressed data.)
		struct stat st;
		if (x_fstat(fd, &st) != 0
		    || (uint64_t)st.st_size != header->content_size) {
			*errmsg = format(
				"Corrupt %s file (actual %lu bytes, expected %lu bytes)",
				name,
				(unsigned long)st.st_size,
				(unsigned long)header->content_size);
			return false;
		}
	}

	return true;
}

void common_header_dump(const struct common_header *header, FILE *f)
{
	fprintf(
		f, "Magic: %c%c%c%c\n",
		header->magic[0], header->magic[1], header->magic[2], header->magic[3]);
	fprintf(f, "Version: %u\n", header->version);
	fprintf(f, "Compression type: %s\n",
	        compression_type_to_string(header->compression_type));
	fprintf(f, "Compression level: %d\n", header->compression_level);
	fprintf(f, "Content size: %" PRIu64 "\n", header->content_size);
}
