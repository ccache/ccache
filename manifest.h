#ifndef MANIFEST_H
#define MANIFEST_H

#include <inttypes.h>
#include "hashtable.h"

struct file_hash
{
	uint8_t hash[16];
	uint32_t size;
};

struct file_hash *manifest_get(const char *manifest_path);
int manifest_put(const char *manifest_path, struct file_hash *object_hash,
                 struct hashtable *included_files);

#endif
