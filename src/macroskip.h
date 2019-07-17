// Copyright (C) 2010-2016 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
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

#ifndef CCACHE_MACROSKIP_H
#define CCACHE_MACROSKIP_H

#include <stdint.h>

// A Boyer-Moore-Horspool skip table used for searching for the strings
// "__TIME__" and "__DATE__".
//
// macro_skip[c] = 8 for all c not in "__TIME__" and "__DATE__".
//
// The other characters map as follows:
//
//   _ -> 1
//   A -> 4
//   D -> 5
//   E -> 2
//   I -> 4
//   M -> 3
//   T -> 3
//
//
// This was generated with the following Python script:
//
// m = {'_': 1,
//      'A': 4,
//      'D': 5,
//      'E': 2,
//      'I': 4,
//      'M': 3,
//      'T': 3}
//
// for i in range(0, 256):
//     if chr(i) in m:
//         num = m[chr(i)]
//     else:
//         num = 8
//     print ("%d, " % num),
//
//     if i % 16 == 15:
//         print ""

static const uint32_t macro_skip[] = {
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  4,  8,  8,  5,  2,  8,  8,  8,  4,  8,  8,  8,  3,  8,  8,
	8,  8,  8,  8,  3,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  1,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
};

#endif
