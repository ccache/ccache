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
		k |= data[1] << 8;
		k |= data[2] << 16;
		k |= data[3] << 24;

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
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
		h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}
