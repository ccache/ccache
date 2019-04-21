#ifndef MDFOUR_H
#define MDFOUR_H

#include <stddef.h>
#include <inttypes.h>

struct mdfour {
	uint32_t A, B, C, D;
	size_t totalN;
	size_t tail_len;
	unsigned char tail[64];
};

void mdfour_begin(struct mdfour *md);
void mdfour_update(struct mdfour *md, const unsigned char *in, size_t n);
void mdfour_result(struct mdfour *md, unsigned char *out);

#endif
