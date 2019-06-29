#ifndef COMPRESSION_H
#define COMPRESSION_H

#include "system.h"

struct compr_state;

struct compressor {
	struct compr_state *(*init)(FILE *output, int8_t compression_level);
	bool (*write)(struct compr_state *state, const void *data, size_t size);
	bool (*free)(struct compr_state *state);
};

struct decompr_state;

struct decompressor {
	struct decompr_state *(*init)(FILE *input);
	bool (*read)(struct decompr_state *state, void *data, size_t size);
	bool (*free)(struct decompr_state *state);
};

enum compression_type {
	COMPR_TYPE_NONE = 0,
	COMPR_TYPE_ZLIB = 1,
	COMPR_TYPE_ZSTD = 2
};

extern struct compressor compressor_none_impl;
extern struct decompressor decompressor_none_impl;

extern struct compressor compressor_zlib_impl;
extern struct decompressor decompressor_zlib_impl;

#ifdef HAVE_LIBZSTD
extern struct compressor compressor_zstd_impl;
extern struct decompressor decompressor_zstd_impl;
#endif // HAVE_LIBZSTD

int8_t compression_level_from_config(void);
enum compression_type compression_type_from_config(void);
const char *compression_type_to_string(uint8_t type);
struct compressor *compressor_from_type(uint8_t type);
struct decompressor *decompressor_from_type(uint8_t type);

#endif
