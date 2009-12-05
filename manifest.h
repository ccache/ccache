#ifndef MANIFEST_H
#define MANIFEST_H

#include "hashutil.h"
#include "hashtable.h"

struct file_hash *manifest_get(const char *manifest_path);
int manifest_put(const char *manifest_path, struct file_hash *object_hash,
                 struct hashtable *included_files);

#endif
