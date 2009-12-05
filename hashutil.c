#include <string.h>

#include "hashutil.h"
#include "murmurhashneutral2.h"

unsigned int hash_from_string(void *str)
{
	return murmurhashneutral2(str, strlen((const char *)str), 0);
}

int strings_equal(void *str1, void *str2)
{
	return strcmp((const char *)str1, (const char *)str2) == 0;
}

int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2)
{
	return memcmp(fh1->hash, fh2->hash, 16) == 0
		&& fh1->size == fh2->size;
}
