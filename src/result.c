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

// Result data format
// ==================
//
// Integers are big-endian.
//
// <result>      ::= <header> <body> <epilogue>
// <header>      ::= <magic> <version> <compr_type> <compr_level> <content_len>
// <magic>       ::= 4 bytes ("cCrS")
// <version>     ::= uint8_t
// <compr_type>  ::= <compr_none> | <compr_zstd>
// <compr_none>  ::= 0 (uint8_t)
// <compr_zstd>  ::= 1 (uint8_t)
// <compr_level> ::= int8_t
// <content_len> ::= uint64_t ; size of file if stored uncompressed
// <body>        ::= <n_entries> <entry>* ; body is potentially compressed
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
// <epilogue>    ::= <checksum>
// <checksum>    ::= uint64_t ; XXH64 of content bytes
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
// checksum        8 bytes
//
//
// Version history
// ===============
//
// 1: Introduced in ccache 3.8.

const char RESULT_MAGIC[4] = "cCrS";

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
result_files_add(struct result_files *list, const char *path,
                 const char *suffix)
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

#define READ_UINT64(var) \
	do { \
		char buf_[8]; \
		READ_BYTES(buf_, sizeof(buf_)); \
		(var) = UINT64_FROM_BYTES(buf_); \
	} while (false)

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
	XXH64_state_t *checksum = XXH64_createState();

	FILE *f = fopen(path, "rb");
	if (!f) {
		// Cache miss.
		*errmsg = x_strdup("No such result file");
		goto out;
	}

	struct common_header header;
	if (!common_header_initialize_for_reading(
		    &header,
		    f,
		    RESULT_MAGIC,
		    RESULT_VERSION,
		    &decompressor,
		    &decompr_state,
		    checksum,
		    errmsg)) {
		goto out;
	}

	if (dump_stream) {
		common_header_dump(&header, dump_stream);
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

		uint64_t filelen;
		READ_UINT64(filelen);

		cc_log("Reading entry #%u: %s (%llu)",
		       i,
		       str_eq(suffix, "stderr") ? "<stderr>" : suffix,
		       (unsigned long long)filelen);

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
					if (!subfile) {
						cc_log("Failed to open %s for writing", list->files[j].path);
						goto out;
					}
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

	if (i != n_entries) {
		*errmsg = format("Too few entries (read %u, expected %u)", i, n_entries);
		goto out;
	}

	uint64_t actual_checksum = XXH64_digest(checksum);
	uint64_t expected_checksum;
	READ_UINT64(expected_checksum);

	if (actual_checksum == expected_checksum) {
		success = true;
	} else {
		*errmsg = format(
			"Incorrect checksum (actual %016llx, expected %016llx)",
			(unsigned long long)actual_checksum,
			(unsigned long long)expected_checksum);
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
	if (checksum) {
		XXH64_freeState(checksum);
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

#define WRITE_UINT64(var) \
	do { \
		char buf_[8]; \
		BYTES_FROM_UINT64(buf_, (var)); \
		WRITE_BYTES(buf_, sizeof(buf_)); \
	} while (false)

static bool
write_result(
	const struct result_files *list,
	struct compressor *compressor,
	struct compr_state *compr_state,
	XXH64_state_t *checksum)
{
	WRITE_BYTE(list->n_files);

	for (uint32_t i = 0; i < list->n_files; i++) {
		cc_log("Writing %s (%llu bytes) to %s",
		       list->files[i].suffix,
		       (unsigned long long)list->files[i].size,
		       list->files[i].path);

		WRITE_BYTE(FILE_MARKER);
		size_t suffix_len = strlen(list->files[i].suffix);
		WRITE_BYTE(suffix_len);
		WRITE_BYTES(list->files[i].suffix, suffix_len);
		WRITE_UINT64(list->files[i].size);

		FILE *f = fopen(list->files[i].path, "rb");
		if (!f) {
			cc_log("Failed to open %s for reading", list->files[i].path);
			goto error;
		}
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

	WRITE_UINT64(XXH64_digest(checksum));

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
	XXH64_state_t *checksum = XXH64_createState();

	char *tmp_file = format("%s.tmp", path);
	int fd = create_tmp_fd(&tmp_file);
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		cc_log("Failed to fdopen %s", tmp_file);
		goto out;
	}

	uint64_t content_size = COMMON_HEADER_SIZE;
	content_size += 1; // n_entries
	for (uint32_t i = 0; i < list->n_files; i++) {
		content_size += 1; // file_marker
		content_size += 1; // suffix_len
		content_size += strlen(list->files[i].suffix); // suffix
		content_size += 8; // data_len
		content_size += list->files[i].size; // data
	}
	content_size += 8; // checksum

	struct common_header header;
	struct compressor *compressor;
	struct compr_state *compr_state;
	if (!common_header_initialize_for_writing(
		    &header,
		    f,
		    RESULT_MAGIC,
		    RESULT_VERSION,
		    compression_type_from_config(),
		    compression_level_from_config(),
		    content_size,
		    checksum,
		    &compressor,
		    &compr_state)) {
		goto out;
	}

	bool ok = write_result(list, compressor, compr_state, checksum)
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
	if (checksum) {
		XXH64_freeState(checksum);
	}
	return ret;
}

bool
result_dump(const char *path, FILE *stream)
{
	char *errmsg;
	bool success = read_result(path, NULL, stream, &errmsg);
	if (errmsg) {
		fprintf(stream, "Error: %s\n", errmsg);
		free(errmsg);
	}
	return success;
}
