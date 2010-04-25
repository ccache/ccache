#ifndef HASHUTIL_H
#define HASHUTIL_H

#include <mdfour.h>
#include <inttypes.h>

struct file_hash
{
	uint8_t hash[16];
	uint32_t size;
};

unsigned int hash_from_string(void *str);
int strings_equal(void *str1, void *str2);
int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2);

enum hash_source_code_result {
	HASH_SOURCE_CODE_OK,
	HASH_SOURCE_CODE_ERROR,
	HASH_SOURCE_CODE_FOUND_VOLATILE_MACRO
};

enum hash_source_code_result
hash_source_code_string(
	struct mdfour *hash, const char *str, size_t len,
	int check_volatile_macros);
enum hash_source_code_result
hash_source_code_file(
	struct mdfour *hash, const char *path,
	int check_volatile_macros);

#endif
