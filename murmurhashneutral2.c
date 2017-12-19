// MurmurHashNeutral2, by Austin Appleby. Released to the public domain. See
// <http://murmurhash.googlepages.com>.

#include "murmurhashneutral2.h"

unsigned int
murmurhashneutral2(const void *key, int len, unsigned int seed)
{
	const unsigned int m = 0x5bd1e995;
	const int r = 24;
	unsigned int h = seed ^ len;
	const unsigned char *data = (const unsigned char *)key;

	while (len >= 4) {
		unsigned int k = data[0];
		k |= ((unsigned int) data[1]) << 8;
		k |= ((unsigned int) data[2]) << 16;
		k |= ((unsigned int) data[3]) << 24;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	switch (len)
	{
	case 3: h ^= ((unsigned int) data[2]) << 16; // fallthrough
	case 2: h ^= ((unsigned int) data[1]) << 8;  // fallthrough
	case 1: h ^= ((unsigned int) data[0]);       // fallthrough
		h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}
