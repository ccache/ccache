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

void hash_include_file_string(
	struct mdfour *hash, const char *str, size_t len);
int hash_include_file(struct mdfour *hash, const char *path);

#endif
