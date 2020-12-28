#ifndef CCACHE_THIRD_PARTY_WIN32_MKTEMP_H_
#define CCACHE_THIRD_PARTY_WIN32_MKTEMP_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int bsd_mkstemp(char *);

// Exposed for testing.
void bsd_mkstemp_set_random_source(void (*)(void *buf, size_t n));

#ifdef __cplusplus
}
#endif
#endif
