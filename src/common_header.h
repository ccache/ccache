#ifndef COMMON_HEADER_H
#define COMMON_HEADER_H

#include "compression.h"
#include "xxhash.h"

#define COMMON_HEADER_SIZE 15

struct common_header {
	char magic[4];
	uint8_t version;
	uint8_t compression_type;
	int8_t compression_level;
	uint64_t content_size;
};

bool common_header_initialize_for_writing(
	struct common_header *header,
	const char magic[4],
	uint8_t version,
	uint64_t content_size,
	XXH64_state_t *checksum,
	struct compressor **compressor,
	struct compr_state **compr_state,
	FILE *output);

bool common_header_initialize_for_reading(
	struct common_header *header,
	FILE *input,
	const char magic[4],
	uint8_t accepted_version,
	struct decompressor **decompressor,
	struct decompr_state **decompr_state,
	XXH64_state_t *checksum,
	char **errmsg);

void common_header_dump(const struct common_header *header, FILE *f);

#endif
