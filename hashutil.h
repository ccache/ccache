#ifndef HASHUTIL_H
#define HASHUTIL_H

#include "mdfour.h"
#include <inttypes.h>

struct file_hash
{
	uint8_t hash[16];
	uint32_t size;
};

unsigned int hash_from_string(void *str);
int strings_equal(void *str1, void *str2);
int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2);

#define HASH_SOURCE_CODE_OK 0
#define HASH_SOURCE_CODE_ERROR 1
#define	HASH_SOURCE_CODE_FOUND_DATE 2
#define	HASH_SOURCE_CODE_FOUND_TIME 4

int hash_source_code_string(
	struct mdfour *hash, const char *str, size_t len, const char *path);
int hash_source_code_file(struct mdfour *hash, const char *path);

#endif
