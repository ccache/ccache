#ifndef COMMON_HEADER_H
#define COMMON_HEADER_H

#define COMMON_HEADER_SIZE 15

struct common_header {
	char magic[4];
	uint8_t version;
	uint8_t compression_type;
	int8_t compression_level;
	uint64_t content_size;
};

void common_header_from_config(
	struct common_header *header,
	const char magic[4],
	uint8_t RESULT_VERSION,
	uint64_t content_size);
void common_header_from_bytes(struct common_header *header, uint8_t *bytes);
void common_header_to_bytes(
	const struct common_header *header, uint8_t *bytes);
bool common_header_verify(
	const struct common_header *header, int fd, const char *name, char **errmsg);
void common_header_dump(const struct common_header *header, FILE *f);

#endif
