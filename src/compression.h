#ifndef COMPRESSION_H
#define COMPRESSION_H

#include "system.h"

struct compr_state;

struct compressor {
	struct compr_state *(*init)(FILE *output, int compression_level);
	bool (*write)(struct compr_state *state, const void *data, size_t size);
	void (*free)(struct compr_state *state);
};

struct decompr_state;

struct decompressor {
	struct decompr_state *(*init)(FILE *input);
	bool (*read)(struct decompr_state *state, void *data, size_t size);
	void (*free)(struct decompr_state *state);
};

extern struct compressor compr_none;
extern struct decompressor decompr_none;

extern struct compressor compr_zlib;
extern struct decompressor decompr_zlib;

#endif
