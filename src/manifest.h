#ifndef MANIFEST_H
#define MANIFEST_H

#include "conf.h"
#include "hashutil.h"
#include "hashtable.h"

extern const char MANIFEST_MAGIC[4];
#define MANIFEST_VERSION 2

struct digest *manifest_get(struct conf *conf, const char *manifest_path);
bool manifest_put(const char *manifest_path, struct digest *result_digest,
                  struct hashtable *included_files);
bool manifest_dump(const char *manifest_path, FILE *stream);

#endif
