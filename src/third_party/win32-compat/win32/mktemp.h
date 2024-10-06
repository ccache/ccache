#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int bsd_mkstemps(char* path, int slen);

// Exposed for testing.
void bsd_mkstemp_set_random_source(void (*)(void* buf, size_t n));

#ifdef __cplusplus
}
#endif
