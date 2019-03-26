#include "backend_memcached.h"
#include "string.h"

void create_backend(const char* name, backend* backend) {
	if (strcmp(name, "MEMCACHED") == 0) {
		backend->from_cache = &backend_memcached_from_cache;
		backend->to_cache = &backend_memcached_to_cache;
		backend->init = &backend_memccached_init;
		backend->done = &backend_memccached_done;
		backend->from_cache_string = &backend_memcached_from_cache_string;
		backend->to_cache_string = &backend_memcached_to_cache_string;
	} else {
		backend = NULL;
	}
}
