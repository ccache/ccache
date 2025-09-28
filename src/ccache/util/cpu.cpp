// Copyright (C) 2024 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "cpu.hpp"

#ifdef _MSC_VER
#  include <intrin.h>
#endif

#ifdef HAVE_CPUID_H
#  include <cpuid.h>
#endif

namespace util {

bool
cpu_supports_avx2()
{
  // CPUID with EAX=7 ECX=0 returns AVX2 support in bit 5 of EBX.
  int registers[4]; // EAX, EBX, ECX, EDX
#if defined(_MSC_VER) && defined(_M_X64)
  __cpuidex(registers, 7, 0);
#elif defined(HAVE_CPUID_H)
  __cpuid_count(7, 0, registers[0], registers[1], registers[2], registers[3]);
#elif __x86_64__
  __asm__ __volatile__("cpuid"
                       : "=a"(registers[0]),
                         "=b"(registers[1]),
                         "=c"(registers[2]),
                         "=d"(registers[3])
                       : "a"(7), "c"(0));
#else
  registers[1] = 0;
#endif
  return registers[1] & (1 << 5);
}

} // namespace util
