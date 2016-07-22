#ifndef CCACHE_MACROSKIP_H
#define CCACHE_MACROSKIP_H

#include <stdint.h>

/*
 * A Boyer-Moore-Horspool skip table used for searching for the strings
 * "__TIME__" and "__DATE__".
 *
 * macro_skip[c] = 8 for all c not in "__TIME__" and "__DATE__".
 *
 * The other characters map as follows:
 *
 *   _ -> 1
 *   A -> 4
 *   D -> 5
 *   E -> 2
 *   I -> 4
 *   M -> 3
 *   T -> 3
 *
 *
 * This was generated with the following Python script:
 *
 * m = {'_': 1,
 *      'A': 4,
 *      'D': 5,
 *      'E': 2,
 *      'I': 4,
 *      'M': 3,
 *      'T': 3}
 *
 * for i in range(0, 256):
 *     if chr(i) in m:
 *         num = m[chr(i)]
 *     else:
 *         num = 8
 *     print ("%d, " % num),
 *
 *     if i % 16 == 15:
 *         print ""
 */

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
