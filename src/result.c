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
#include "common_header.h"
#include "int_bytes_conversion.h"
#include "compression.h"
#include "result.h"

// Result data format (big-endian integers):
//
// <result>      ::= <header> <body>
// <header>      ::= <magic> <version> <compr_type> <compr_level> <content_len>
// <body>        ::= <n_entries> <entry>* ; body is potentially compressed
// <magic>       ::= 4 bytes ("cCrS")
// <version>     ::= uint8_t
// <compr_type>  ::= <compr_none> | <compr_zlib> | <compr_zstd>
// <compr_none>  ::= 0 (uint8_t)
// <compr_zlib>  ::= 1 (uint8_t)
// <compr_zstd>  ::= 2 (uint8_t)
// <compr_level> ::= int8_t
// <content_len> ::= uint64_t ; size of file if stored uncompressed
// <n_entries>   ::= uint8_t
// <entry>       ::= <file_entry> | <ref_entry>
// <file_entry>  ::= <file_marker> <suffix_len> <suffix> <data_len> <data>
// <file_marker> ::= 0 (uint8_t)
// <suffix_len>  ::= uint8_t
// <suffix>      ::= suffix_len bytes
// <data_len>    ::= uint64_t
// <data>        ::= data_len bytes
// <ref_entry>   ::= <ref_marker> <key_len> <key>
// <ref_marker>  ::= 1 (uint8_t)
// <key_len>     ::= uint8_t
// <key>         ::= key_len bytes
//
// Sketch of concrete layout:
//
// <magic>         4 bytes
// <version>       1 byte
// <compr_type>    1 byte
// <compr_level>   1 byte
// <content_len>   8 bytes
// --- [potentially compressed from here] -------------------------------------
// <n_entries>     1 byte
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
//
// Version history:
//
// 1: Introduced in ccache 3.8.

static const char MAGIC[4] = "cCrS";

enum {
	FILE_MARKER = 0,
	REF_MARKER = 1
};

struct result_file {
	char *suffix;
	char *path;
	uint64_t size;
};

struct result_files {
	uint32_t n_files;
	struct result_file *files;
	uint64_t *sizes;
};

struct result_files *
result_files_init(void)
{
	struct result_files *list = x_malloc(sizeof(*list));
	list->n_files = 0;
	list->files = NULL;
	list->sizes = NULL;

	return list;
}

void
result_files_add(struct result_files *list, const char *path, const char *suffix)
{
	uint32_t n = list->n_files;
	list->files = x_realloc(list->files, (n + 1) * sizeof(*list->files));
	list->sizes = x_realloc(list->sizes, (n + 1) * sizeof(*list->sizes));
	struct result_file *f = &list->files[list->n_files];
	list->n_files++;

	struct stat st;
	x_stat(path, &st);

	f->suffix = x_strdup(suffix);
	f->path = x_strdup(path);
	f->size = st.st_size;
}

void
result_files_free(struct result_files *list)
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

static bool
read_result(
	const char *path,
	struct result_files *list,
	FILE *dump_stream,
	char **errmsg)
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

	struct common_header header;
	if (!common_header_from_file(&header, f)) {
		*errmsg = format("Failed to read header from %s", path);
		goto out;
	}

	if (memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0) {
		*errmsg = format(
			"Result file has bad magic value 0x%x%x%x%x",
			header.magic[0], header.magic[1], header.magic[2], header.magic[3]);
		goto out;
	}

	if (dump_stream) {
		fprintf(dump_stream, "Magic: %c%c%c%c\n",
		        MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3]);
		fprintf(dump_stream, "Version: %u\n", header.version);
		fprintf(dump_stream, "Compression type: %s\n",
		        compression_type_to_string(header.compression_type));
		fprintf(dump_stream, "Compression level: %d\n", header.compression_level);
		fprintf(dump_stream, "Content size: %" PRIu64 "\n", header.content_size);
	}

	if (header.version != RESULT_VERSION) {
		*errmsg = format(
			"Unknown result version (actual %u, expected %u)",
			header.version,
			RESULT_VERSION);
		goto out;
	}

	if (header.compression_type == COMPR_TYPE_NONE) {
		// Since we have the size available, let's use it as a super primitive
		// consistency check for the non-compressed case. (A real checksum is used
		// for compressed data.)
		struct stat st;
		if (x_fstat(fileno(f), &st) != 0
		    || (uint64_t)st.st_size != header.content_size) {
			*errmsg = format(
				"Corrupt result file (actual %lu bytes, expected %lu bytes)",
				(unsigned long)st.st_size,
				(unsigned long)header.content_size);
			goto out;
		}
	}

	decompressor = decompressor_from_type(header.compression_type);
	if (!decompressor) {
		*errmsg = format("Unknown compression type: %u", header.compression_type);
		goto out;
	}

	decompr_state = decompressor->init(f);
	if (!decompr_state) {
		*errmsg = x_strdup("Failed to initialize decompressor");
		goto out;
	}

	uint8_t n_entries;
	READ_BYTE(n_entries);

	uint32_t i;
	for (i = 0; i < n_entries; i++) {
		uint8_t marker;
		READ_BYTE(marker);
		switch (marker) {
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

		char filelen_buffer[8];
		READ_BYTES(filelen_buffer, sizeof(filelen_buffer));
		uint64_t filelen = UINT64_FROM_BYTES(filelen_buffer);

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

	if (i == n_entries) {
		success = true;
	} else {
		*errmsg = format("Too few entries (read %u, expected %u)", i, n_entries);
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
	const struct result_files *list,
	struct compressor *compressor,
	struct compr_state *compr_state)
{
	WRITE_BYTE(list->n_files);

	for (uint32_t i = 0; i < list->n_files; i++) {
		cc_log("Writing file #%u: %s (%lu)", i, list->files[i].suffix,
		       (unsigned long)list->files[i].size);

		WRITE_BYTE(FILE_MARKER);
		size_t suffix_len = strlen(list->files[i].suffix);
		WRITE_BYTE(suffix_len);
		WRITE_BYTES(list->files[i].suffix, suffix_len);
		WRITE_INT(8, list->files[i].size);

		FILE *f = fopen(list->files[i].path, "rb");
		char buf[READ_BUFFER_SIZE];
		size_t remain = list->files[i].size;
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

	return true;

error:
	cc_log("Error writing to result file");
	return false;
}

bool result_get(const char *path, struct result_files *list)
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

bool result_put(const char *path, struct result_files *list)
{
	bool ret = false;
	char *tmp_file = format("%s.tmp", path);
	int fd = create_tmp_fd(&tmp_file);
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		cc_log("Failed to fdopen %s", tmp_file);
		goto out;
	}

	int8_t compr_level = compression_level_from_config();
	enum compression_type compr_type = compression_type_from_config();

	struct common_header header;
	memcpy(header.magic, MAGIC, sizeof(MAGIC));
	header.version = RESULT_VERSION;
	header.compression_type = compr_type;
	header.compression_level = compr_level;
	uint64_t content_size = COMMON_HEADER_SIZE;
	content_size += 1; // n_entries
	for (uint32_t i = 0; i < list->n_files; i++) {
		content_size += 1; // file_marker
		content_size += 1; // suffix_len
		content_size += strlen(list->files[i].suffix); // suffix
		content_size += 8; // data_len
		content_size += list->files[i].size; // data
	}
	header.content_size = content_size;

	if (!common_header_to_file(&header, f)) {
		cc_log("Failed to write result file header to %s", tmp_file);
		goto out;
	}

	struct compressor *compressor = compressor_from_type(compr_type);
	assert(compressor);
	struct compr_state *compr_state = compressor->init(f, compr_level);
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
