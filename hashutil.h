#ifndef HASHUTIL_H
#define HASHUTIL_H

#include <inttypes.h>

struct file_hash
{
	uint8_t hash[16];
	uint32_t size;
};

unsigned int hash_from_string(void *str);
int strings_equal(void *str1, void *str2);
int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2);

#endif
