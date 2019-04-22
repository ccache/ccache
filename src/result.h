#ifndef STORAGE_H
#define STORAGE_H

#include "conf.h"

#define USE_SINGLE 0
#define USE_AGGREGATED 1

struct cache *create_empty_cache(void);

int add_cache_file(struct cache *c, const char *path, const char *suffix);

void free_cache(struct cache *c);


bool cache_get(const char *cache_path, struct cache *c);
bool cache_put(const char *cache_path, struct cache *c);

#endif
