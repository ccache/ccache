#define COMMON_HEADER_SIZE 15

struct common_header
{
	char magic[4];
	uint8_t version;
	uint8_t compression_type;
	int8_t compression_level;
	uint64_t content_size;
};


bool common_header_from_file(struct common_header *header, FILE *f);
bool common_header_to_file(const struct common_header *header, FILE *f);
