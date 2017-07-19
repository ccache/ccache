#ifndef MDFOUR_H
#define MDFOUR_H

#include <stddef.h>
#ifdef HAVE_INTTYPES_H
#   include <inttypes.h>
#endif

struct mdfour {
	uint32_t A, B, C, D;
	size_t totalN;
	unsigned char tail[64];
	size_t tail_len;
	int finalized;
};

void mdfour_begin(struct mdfour *md);
void mdfour_update(struct mdfour *md, const unsigned char *in, size_t n);
void mdfour_result(struct mdfour *md, unsigned char *out);

#endif
