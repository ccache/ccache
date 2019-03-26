#ifndef BACKEND_MEMCACHED_H
#define BACKEND_MEMCACHED_H

#include "backend.h"

int backend_memcached_from_cache(const char* id, backend_load *load);
int backend_memcached_to_cache(const char* id, backend_load *load);
int backend_memcached_from_cache_string(const char* id, char** string, size_t* size);
int backend_memcached_to_cache_string(const char* id, char* string, size_t size);
void backend_memccached_done(void);
void backend_memccached_init(void* configuration);

#endif
