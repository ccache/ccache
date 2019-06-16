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

#ifdef HAVE_LIBZSTD
#include "zstd_zlibwrapper.h"
#endif

// Result data format:
//
// <result>      ::= <header> <body> ; <body> is potentially compressed
// <header>      ::= <magic> <version> <compr_type> <compr_level>
// <body>        ::= <entry>* <eof_marker>
// <eof_marker>  ::= 0 (uint8_t)
// <magic>       ::= uint32_t ; "cCrS"
// <version>     ::= uint8_t
// <compr_type>  ::= <compr_none> | <compr_zlib> | <compr_zstd>
// <compr_none>  ::= 0
// <compr_zlib>  ::= 1
// <compr_zstd>  ::= 2
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
	COMPR_TYPE_ZLIB = 1,
	COMPR_TYPE_ZSTD = 2
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
filelist_init(void)
{
	struct filelist *list = x_malloc(sizeof(*list));
	list->n_files = 0;
	list->files = NULL;
	list->sizes = NULL;

	return list;
}

int
filelist_add(struct filelist *list, const char *path, const char *suffix)
{
	uint32_t n = list->n_files;
	list->files = x_realloc(list->files, (n + 1) * sizeof(*list->files));
	list->sizes = x_realloc(list->sizes, (n + 1) * sizeof(*list->sizes));
	struct file *f = &list->files[list->n_files];
	list->n_files++;

	f->suffix = x_strdup(suffix);
	f->path_len = strlen(path);
	f->path = x_strdup(path);

	return n;
}

void
filelist_free(struct filelist *list)
{
	for (uint32_t i = 0; i < list->n_files; i++) {
		free(list->files[i].suffix);
		free(list->files[i].path);
	}
	free(list->files);
	list->files = NULL;
	free(list->sizes);
	list->sizes = NULL;

	free(list);
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
read_result(
	const char *path, struct filelist *list, FILE *dump_stream, char **errmsg)
{
	*errmsg = NULL;
	bool success = false;
	struct decompressor *decompressor = NULL;
	struct decompr_state *decompr_state = NULL;
	FILE *subfile = NULL;

	FILE *f = fopen(path, "rb");
	if (!f) {
		// Cache miss.
		*errmsg = x_strdup("No such result file");
		goto out;
	}

	char header[7];
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		*errmsg = x_strdup("Failed to read result file header");
		goto out;
	}

	if (memcmp(header, MAGIC, sizeof(MAGIC)) != 0) {
		*errmsg = format(
			"Result file has bad magic value 0x%x%x%x%x",
			header[0], header[1], header[2], header[3]);
		goto out;
	}

	const uint8_t version = header[4];
	if (version != RESULT_VERSION) {
		*errmsg = format(
			"Unknown result version (actual %u, expected %u)",
			version,
			RESULT_VERSION);
		goto out;
	}

	const uint8_t compr_type = header[5];
	const char* compr_type_name;

	switch (compr_type) {
	case COMPR_TYPE_NONE:
		compr_type_name = "none";
		decompressor = &decompr_none;
		break;

	case COMPR_TYPE_ZLIB:
		compr_type_name = "zlib";
		decompressor = &decompr_zlib;
		break;

#ifdef HAVE_LIBZSTD
	case COMPR_TYPE_ZSTD:
		compr_type_name = "zstd";
		ZWRAP_setDecompressionType(ZWRAP_AUTO);
		decompressor = &decompr_zlib;
		break;
#endif

	default:
		*errmsg = format("Unknown compression type: %u", compr_type);
		goto out;
	}

	decompr_state = decompressor->init(f);
	if (!decompr_state) {
		*errmsg = x_strdup("Failed to initialize decompressor");
		goto out;
	}

	if (dump_stream) {
		const uint8_t compr_level = header[6];
		fprintf(dump_stream, "Magic: %c%c%c%c\n",
		        MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3]);
		fprintf(dump_stream, "Version: %u\n", version);
		fprintf(dump_stream, "Compression type: %s\n", compr_type_name);
		fprintf(dump_stream, "Compression level: %u\n", compr_level);
	}

	uint8_t marker;
	for (uint32_t i = 0;; i++) {
		READ_BYTE(marker);
		switch (marker) {
		case EOF_MARKER:
			success = true;
			goto out;

		case FILE_MARKER:
			break;

		case REF_MARKER:
			// TODO: Implement.
			// Fall through.

		default:
			*errmsg = format("Unknown entry type: %u", marker);
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
			for (uint32_t j = 0; j < list->n_files; j++) {
				if (str_eq(suffix, list->files[j].suffix)) {
					found = true;

					cc_log("Copying to %s", list->files[j].path);

					subfile = fopen(list->files[j].path, "wb");
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
	if (decompressor && !decompressor->free(decompr_state)) {
		success = false;
	}
	if (f) {
		fclose(f);
	}
	if (!success && !*errmsg) {
		*errmsg = x_strdup("Corrupt result file");
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
write_result(
	const struct filelist *list,
	struct compressor *compressor,
	struct compr_state *compr_state)
{
	for (uint32_t i = 0; i < list->n_files; i++) {
		struct stat st;
		if (x_stat(list->files[i].path, &st) != 0) {
			return false;
		}

		cc_log("Writing file #%u: %s (%lu)", i, list->files[i].suffix,
		       (unsigned long)st.st_size);

		WRITE_BYTE(FILE_MARKER);
		size_t suffix_len = strlen(list->files[i].suffix);
		WRITE_BYTE(suffix_len);
		WRITE_BYTES(list->files[i].suffix, suffix_len);
		WRITE_INT(8, st.st_size);

		FILE *f = fopen(list->files[i].path, "rb");
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
	cc_log("Error writing to result file");
	return false;
}

bool result_get(const char *path, struct filelist *list)
{
	cc_log("Getting result %s", path);

	char *errmsg;
	bool success = read_result(path, list, NULL, &errmsg);
	if (errmsg) {
		cc_log("Error: %s", errmsg);
		free(errmsg);
	}
	if (success) {
		// Update modification timestamp to save files from LRU cleanup.
		update_mtime(path);
	}
	return success;
}

bool result_put(const char *path, struct filelist *list, int compression_level)
{
	bool ret = false;
	char *tmp_file = format("%s.tmp", path);
	int fd = create_tmp_fd(&tmp_file);
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		cc_log("Failed to fdopen %s", tmp_file);
		goto out;
	}

	uint8_t compr_type;
	if (compression_level == 0) {
		compr_type = COMPR_TYPE_NONE;
	} else {
#ifdef HAVE_LIBZSTD
		compr_type = COMPR_TYPE_ZSTD;
#else
		compr_type = COMPR_TYPE_ZLIB;
#endif
	}

	char header[7];
	memcpy(header, MAGIC, sizeof(MAGIC));
	header[4] = RESULT_VERSION;
	header[5] = compr_type;
	header[6] = compression_level;
	if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
		cc_log("Failed to write to %s", tmp_file);
		goto out;
	}

	struct compressor *compressor;
	switch (compr_type) {
	case COMPR_TYPE_NONE:
		compressor = &compr_none;
		break;

	case COMPR_TYPE_ZLIB:
		compressor = &compr_zlib;
		break;

#ifdef HAVE_LIBZSTD
	case COMPR_TYPE_ZSTD:
		ZWRAP_useZSTDcompression(1);
		compressor = &compr_zlib;
		break;
#endif
	}

	struct compr_state *compr_state = compressor->init(f, compression_level);
	if (!compr_state) {
		cc_log("Failed to initialize compressor");
		goto out;
	}
	bool ok = write_result(list, compressor, compr_state)
		&& compressor->free(compr_state);
	if (!ok) {
		cc_log("Failed to write result file");
		goto out;
	}

	fclose(f);
	f = NULL;
	if (x_rename(tmp_file, path) == 0) {
		ret = true;
	} else {
		cc_log("Failed to rename %s to %s", tmp_file, path);
	}

out:
	free(tmp_file);
	if (f) {
		fclose(f);
	}
	return ret;
}

bool
result_dump(const char *path, FILE *stream)
{
	char *errmsg;
	bool success = read_result(path, NULL, stream, &errmsg);
	if (errmsg) {
		fprintf(stderr, "Error: %s\n", errmsg);
		free(errmsg);
	}
	return success;
}
