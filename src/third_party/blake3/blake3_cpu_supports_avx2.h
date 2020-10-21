#ifndef BLAKE3_CPU_SUPPORTS_AVX2_H
#define BLAKE3_CPU_SUPPORTS_AVX2_H

// This file is a ccache modification to BLAKE3

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool blake3_cpu_supports_avx2();

#ifdef __cplusplus
}
#endif

#endif
