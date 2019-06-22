#define COMMON_HEADER_SIZE 15

struct common_header {
	char magic[4];
	uint8_t version;
	uint8_t compression_type;
	int8_t compression_level;
	uint64_t content_size;
};

void common_header_init_from_config(
	struct common_header *header,
	const char magic[4],
	uint8_t RESULT_VERSION,
	uint64_t content_size);
bool common_header_init_from_file(struct common_header *header, FILE *f);
bool common_header_write_to_file(const struct common_header *header, FILE *f);
bool common_header_verify(
	const struct common_header *header, int fd, const char *name, char **errmsg);
void common_header_dump(const struct common_header *header, FILE *f);
