// This file is a ccache modification to BLAKE3

#include "blake3_dispatch.c"

#include "blake3_cpu_supports_avx2.h"

bool blake3_cpu_supports_avx2()
{
  return get_cpu_features() & AVX2;
}
