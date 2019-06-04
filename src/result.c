// Copyright (C) 2019 Joel Rosdahl
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

#include "ccache.h"
#include "compression.h"
#include "result.h"

// Result data format:
//
// <result>      ::= <header> <body> ; <body> is potentially compressed
// <header>      ::= <magic> <version> <compr_type> <compr_level>
// <body>        ::= <entry>* <eof_marker>
// <eof_marker>  ::= 0 (uint8_t)
// <magic>       ::= uint32_t ; "cCrS"
// <version>     ::= uint8_t
// <compr_type>  ::= <compr_none> | <compr_zlib>
// <compr_none>  ::= 0
// <compr_zlib>  ::= 1
// <compr_level> ::= uint8_t
// <entry>       ::= <file_entry> | <ref_entry>
// <file_entry>  ::= <file_marker> <suffix_len> <suffix> <data_len> <data>
// <file_marker> ::= 1 (uint8_t)
// <suffix_len>  ::= uint8_t
// <suffix>      ::= suffix_len bytes
// <data_len>    ::= uint64_t
// <data>        ::= data_len bytes
// <ref_entry>   ::= <ref_marker> <key_len> <key>
// <ref_marker>  ::= 2 (uint8_t)
// <key_len>     ::= uint8_t
// <key>         ::= key_len bytes
//
// Sketch of concrete layout:
//
// <magic>         4 bytes
// <version>       1 byte
// <compr_type>    1 byte
// <compr_level>   1 byte
// --- [potentially compressed from here ] -----------------------------------
// <file_marker>   1 byte
// <suffix_len>    1 byte
// <suffix>        suffix_len bytes
// <data_len>      8 bytes
// <data>          data_len bytes
// ...
// <ref_marker>    1 byte
// <key_len>       1 byte
// <key>           key_len bytes
// ...
// <eof_marker>    1 byte

static const char MAGIC[4] = "cCrS";

enum {
	EOF_MARKER = 0,
	FILE_MARKER = 1,
	REF_MARKER = 2
};

enum {
	COMPR_TYPE_NONE = 0,
	COMPR_TYPE_ZLIB = 1
};

struct file {
	char *suffix;
	uint32_t path_len;
	char *path;
};

struct filelist {
	uint32_t n_files;
	struct file *files;
	uint64_t *sizes;
};

struct filelist *
create_empty_filelist(void)
{
	struct filelist *l = x_malloc(sizeof(*l));
	l->n_files = 0;
	l->files = NULL;
	l->sizes = NULL;

	return l;
}

int
add_file_to_filelist(struct filelist *l, const char *path, const char *suffix)
{
	uint32_t n = l->n_files;
	l->files = x_realloc(l->files, (n + 1) * sizeof(*l->files));
	l->sizes = x_realloc(l->sizes, (n + 1) * sizeof(*l->sizes));
	struct file *f = &l->files[l->n_files];
	l->n_files++;

	f->suffix = x_strdup(suffix);
	f->path_len = strlen(path);
	f->path = x_strdup(path);

	return n;
}

void
free_filelist(struct filelist *l)
{
	for (uint32_t i = 0; i < l->n_files; i++) {
		free(l->files[i].suffix);
		free(l->files[i].path);
	}
	free(l->files);
	l->files = NULL;
	free(l->sizes);
	l->sizes = NULL;

	free(l);
}
#define READ_BYTES(buf, length) \
	do { \
		if (!decompressor->read(decompr_state, buf, length)) { \
			goto out; \
		} \
	} while (false)

#define READ_BYTE(var) \
	READ_BYTES(&var, 1)

#define READ_INT(size, var) \
	do { \
		uint8_t buf_[size]; \
		READ_BYTES(buf_, size); \
		uint64_t u_ = 0; \
		for (size_t i_ = 0; i_ < (size); i_++) { \
			u_ <<= 8; \
			u_ |= buf_[i_]; \
		} \
		(var) = u_; \
	} while (false)

static bool
read_cache(const char *path, struct filelist *l, FILE *dump_stream)
{
	bool success = false;
	struct decompressor *decompressor = NULL;
	struct decompr_state *decompr_state = NULL;
	FILE *subfile = NULL;

	FILE *f = fopen(path, "rb");
	if (!f) {
		// Cache miss.
		cc_log("No such cache file");
		goto out;
	}

	char header[7];
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		cc_log("Failed to read result file header");
		goto out;
	}

	if (memcmp(header, MAGIC, sizeof(MAGIC)) != 0) {
		cc_log("Cache file has bad magic value 0x%x%x%x%x",
		       header[0], header[1], header[2], header[3]);
		// TODO: Return error message like read_manifest does.
		goto out;
	}

	// TODO: Verify version like read_manifest does.
	const uint8_t version = header[4];
	const uint8_t compr_type = header[5];

	switch (compr_type) {
	case COMPR_TYPE_NONE:
		decompressor = &decompr_none;
		break;

	case COMPR_TYPE_ZLIB:
		decompressor = &decompr_zlib;
		break;

	default:
		cc_log("Unknown compression type: %u", compr_type);
		goto out;
	}

	decompr_state = decompressor->init(f);

	if (dump_stream) {
		const uint8_t compr_level = header[6];
		fprintf(dump_stream, "Magic: %c%c%c%c\n",
		        MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3]);
		fprintf(dump_stream, "Version: %u\n", version);
		fprintf(dump_stream, "Compression type: %s\n",
		        compr_type == COMPR_TYPE_NONE ? "none" : "zlib");
		fprintf(dump_stream, "Compression level: %u\n", compr_level);
	}

	uint8_t marker;
	for (uint32_t i = 0; ; i++) {
		READ_BYTE(marker);
		switch (marker) {
		case EOF_MARKER:
			success = true;
			goto out;

		case FILE_MARKER:
			break;

		case REF_MARKER:
			// TODO: Implement.
			continue;

		default:
			cc_log("Unknown entry type: %u", marker);
			goto out;
		}

		uint8_t suffix_len;
		READ_BYTE(suffix_len);

		char suffix[256 + 1];
		READ_BYTES(suffix, suffix_len);
		suffix[suffix_len] = '\0';

		uint64_t filelen;
		READ_INT(8, filelen);

		cc_log("Reading entry #%u: %s (%lu)",
		       i,
		       str_eq(suffix, "stderr") ? "<stderr>" : suffix,
		       (unsigned long)filelen);

		bool found = false;
		if (dump_stream) {
			fprintf(dump_stream,
			        "Entry: %s (size: %" PRIu64 " bytes)\n",
			        str_eq(suffix, "stderr") ? "<stderr>" : suffix,
			        filelen);
		} else {
			for (uint32_t j = 0; j < l->n_files; j++) {
				if (str_eq(suffix, l->files[j].suffix)) {
					found = true;

					cc_log("Copying to %s", l->files[j].path);

					subfile = fopen(l->files[j].path, "wb");
					char buf[READ_BUFFER_SIZE];
					size_t remain = filelen;
					while (remain > 0) {
						size_t n = MIN(remain, sizeof(buf));
						READ_BYTES(buf, n);
						if (fwrite(buf, 1, n, subfile) != n) {
							goto out;
						}
						remain -= n;
					}
					fclose(subfile);
					subfile = NULL;
				}
			}
		}
		if (!found) {
			// Discard the file data.
			char buf[READ_BUFFER_SIZE];
			size_t remain = filelen;
			while (remain > 0) {
				size_t n = MIN(remain, sizeof(buf));
				READ_BYTES(buf, n);
				remain -= n;
			}
		}
	}

out:
	if (subfile) {
		fclose(subfile);
	}
	if (decompressor) {
		decompressor->free(decompr_state);
	}
	if (f) {
		fclose(f);
	}
	if (!success) {
		cc_log("Corrupt cache file");
	}
	return success;
}

#define WRITE_BYTES(buf, length) \
	do { \
		if (!compressor->write(compr_state, buf, length)) { \
			goto error; \
		} \
	} while (false)

#define WRITE_BYTE(var) \
	do { \
		char ch_ = var; \
		WRITE_BYTES(&ch_, 1); \
	} while (false)

#define WRITE_INT(size, var) \
	do { \
		uint64_t u_ = (var); \
		uint8_t buf_[size]; \
		for (size_t i_ = 0; i_ < (size); i_++) { \
			buf_[i_] = (u_ >> (8 * ((size) - i_ - 1))); \
		} \
		WRITE_BYTES(buf_, size); \
	} while (false)

static bool
write_cache(
	const struct filelist *l,
	struct compressor *compressor,
	struct compr_state *compr_state)
{
	for (uint32_t i = 0; i < l->n_files; i++) {
		struct stat st;
		if (x_stat(l->files[i].path, &st) != 0) {
			return false;
		}

		cc_log("Writing file #%u: %s (%lu)", i, l->files[i].suffix,
		       (unsigned long)st.st_size);

		WRITE_BYTE(FILE_MARKER);
		size_t suffix_len = strlen(l->files[i].suffix);
		WRITE_BYTE(suffix_len);
		WRITE_BYTES(l->files[i].suffix, suffix_len);
		WRITE_INT(8, st.st_size);

		FILE *f = fopen(l->files[i].path, "rb");
		char buf[READ_BUFFER_SIZE];
		size_t remain = st.st_size;
		while (remain > 0) {
			size_t n = MIN(remain, sizeof(buf));
			if (fread(buf, 1, n, f) != n) {
				goto error;
			}
			WRITE_BYTES(buf, n);
			remain -= n;
		}
		fclose(f);
	}

	WRITE_BYTE(EOF_MARKER);

	return true;

error:
	cc_log("Error writing to cache file");
	return false;
}

bool cache_get(const char *path, struct filelist *l)
{
	cc_log("Getting result %s from cache", path);
	return read_cache(path, l, NULL);
}

bool cache_put(const char *cache_path, struct filelist *l, int compression_level)
{
	bool ret = false;
	char *tmp_file = format("%s.tmp", cache_path);
	int fd = create_tmp_fd(&tmp_file);
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		cc_log("Failed to fdopen %s", tmp_file);
		goto out;
	}

	char header[7];
	memcpy(header, MAGIC, sizeof(MAGIC));
	header[4] = RESULT_VERSION;
	header[5] = compression_level == 0 ? COMPR_TYPE_NONE : COMPR_TYPE_ZLIB;
	header[6] = compression_level;
	if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
		cc_log("Failed to write to %s", tmp_file);
		goto out;
	}

	struct compressor *compressor;
	if (compression_level == 0) {
		compressor = &compr_none;
	} else {
		compressor = &compr_zlib;
	}

	struct compr_state *compr_state =
		compressor->init(f, compression_level);
	bool ok = write_cache(l, compressor, compr_state);
	compressor->free(compr_state);
	if (!ok) {
		cc_log("Failed to write cache file");
		goto out;
	}

	fclose(f);
	f = NULL;
	if (x_rename(tmp_file, cache_path) == 0) {
		ret = true;
	} else {
		cc_log("Failed to rename %s to %s", tmp_file, cache_path);
	}

out:
	free(tmp_file);
	if (f) {
		fclose(f);
	}
	return ret;
}

bool
cache_dump(const char *cache_path, FILE *stream)
{
	return read_cache(cache_path, NULL, stream);
}
