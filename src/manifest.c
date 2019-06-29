// Copyright (C) 2009-2019 Joel Rosdahl
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
#include "hashtable_itr.h"
#include "hashutil.h"
#include "common_header.h"
#include "compression.h"
#include "int_bytes_conversion.h"
#include "manifest.h"
#include "xxhash.h"

// Manifest data format (big-endian integers):
//
// <manifest>      ::= <header> <body>
// <header>        ::= <magic> <version> <compr_type> <compr_level>
//                     <content_len>
// <magic>         ::= 4 bytes ("cCrS")
// <version>       ::= uint8_t
// <compr_type>    ::= <compr_none> | <compr_zlib>
// <compr_none>    ::= 0 (uint8_t)
// <compr_zlib>    ::= 1 (uint8_t)
// <compr_level>   ::= int8_t
// <content_len>   ::= uint64_t ; size of file if stored uncompressed
// <body>          ::= <paths> <includes> <results> ; body is potentially
//                                                  ; compressed
// <paths>         ::= <n_paths> <path_entry>*
// <n_paths>       ::= uint32_t
// <path_entry>    ::= <path_len> <path>
// <path_len>      ::= uint16_t
// <path>          ::= path_len bytes
// <includes>      ::= <n_includes> <include_entry>*
// <n_includes>    ::= uint32_t
// <include_entry> ::= <path_index> <digest> <fsize> <mtime> <ctime>
// <path_index>    ::= uint32_t
// <digest>        ::= DIGEST_SIZE bytes
// <fsize>         ::= uint64_t ; file size
// <mtime>         ::= int64_t ; modification time
// <ctime>         ::= int64_t ; status change time
// <results>       ::= <n_results> <result>*
// <n_results>     ::= uint32_t
// <result>        ::= <n_indexes> <include_index>* <name>
// <n_indexes>     ::= uint32_t
// <include_index> ::= uint32_t
// <name>          ::= DIGEST_SIZE bytes
//
// Sketch of concrete layout:

// <magic>         4 bytes
// <version>       1 byte
// <compr_type>    1 byte
// <compr_level>   1 byte
// <content_len>   8 bytes
// --- [potentially compressed from here] -------------------------------------
// <n_paths>       4 bytes
// <path_len>      2 bytes
// <path>          path_len bytes
// ...
// ----------------------------------------------------------------------------
// <n_includes>    4 bytes
// <path_index>    4 bytes
// <digest>        DIGEST_SIZE bytes
// <fsize>         8 bytes
// <mtime>         8 bytes
// <ctime>         8 bytes
// ...
// ----------------------------------------------------------------------------
// <n_results>     4 bytes
// <n_indexes>     4 bytes
// <include_index> 4 bytes
// ...
// <name>          DIGEST_SIZE bytes
// ...
//
// Version history:
//
// 1: Introduced in ccache 3.0. (Files are always compressed with gzip.)
// 2: Introduced in ccache 3.8.

static const char MAGIC[4] = "cCmF";
static const uint32_t MAX_MANIFEST_ENTRIES = 100;
static const uint32_t MAX_MANIFEST_FILE_INFO_ENTRIES = 10000;

#define ccache_static_assert(e) \
	do { enum { ccache_static_assert__ = 1/(e) }; } while (false)

struct file {
	uint16_t path_len; // strlen(path)
	char *path; // NUL-terminated
};

struct file_info {
	// Index to n_files.
	uint32_t index;
	// Digest of referenced file.
	struct digest digest;
	// Size of referenced file.
	uint64_t fsize;
	// mtime of referenced file.
	int64_t mtime;
	// ctime of referenced file.
	int64_t ctime;
};

struct result {
	// Number of entries in file_info_indexes.
	uint32_t n_file_info_indexes;
	// Indexes to file_infos.
	uint32_t *file_info_indexes;
	// Name of the result.
	struct digest name;
};

struct manifest {
	struct common_header header;

	// Referenced include files.
	uint32_t n_files;
	struct file *files;

	// Information about referenced include files.
	uint32_t n_file_infos;
	struct file_info *file_infos;

	// Result names plus references to include file infos.
	uint32_t n_results;
	struct result *results;
};

struct file_stats {
	uint64_t size;
	int64_t mtime;
	int64_t ctime;
};

static unsigned int
hash_from_file_info(void *key)
{
	ccache_static_assert(sizeof(struct file_info) == 48); // No padding.
	return XXH64(key, sizeof(struct file_info), 0);
}

static int
file_infos_equal(void *key1, void *key2)
{
	struct file_info *fi1 = (struct file_info *)key1;
	struct file_info *fi2 = (struct file_info *)key2;
	return fi1->index == fi2->index
	       && digests_equal(&fi1->digest, &fi2->digest)
	       && fi1->fsize == fi2->fsize
	       && fi1->mtime == fi2->mtime
	       && fi1->ctime == fi2->ctime;
}

static void
free_manifest(struct manifest *mf)
{
	for (uint32_t i = 0; i < mf->n_files; i++) {
		free(mf->files[i].path);
	}
	free(mf->files);
	free(mf->file_infos);
	for (uint32_t i = 0; i < mf->n_results; i++) {
		free(mf->results[i].file_info_indexes);
	}
	free(mf->results);
	free(mf);
}

#define READ_BYTES(buf, length) \
	do { \
		if (!decompressor->read(decompr_state, buf, length)) { \
			goto out; \
		} \
	} while (false)

#define READ_UINT16(var) \
	do { \
		char buf_[2]; \
		READ_BYTES(buf_, sizeof(buf_)); \
		(var) = UINT16_FROM_BYTES(buf_); \
	} while (false)

#define READ_UINT32(var) \
	do { \
		char buf_[4]; \
		READ_BYTES(buf_, sizeof(buf_)); \
		(var) = UINT32_FROM_BYTES(buf_); \
	} while (false)

#define READ_INT64(var) \
	do { \
		char buf_[8]; \
		READ_BYTES(buf_, sizeof(buf_)); \
		(var) = INT64_FROM_BYTES(buf_); \
	} while (false)

#define READ_UINT64(var) \
	do { \
		char buf_[8]; \
		READ_BYTES(buf_, sizeof(buf_)); \
		(var) = UINT64_FROM_BYTES(buf_); \
	} while (false)

#define READ_STR(str_var, len_var) \
	do { \
		READ_UINT16(len_var); \
		(str_var) = x_malloc(len_var + 1); \
		READ_BYTES(str_var, len_var); \
		str_var[len_var] = '\0'; \
	} while (false)

static struct manifest *
create_empty_manifest(void)
{
	struct manifest *mf = x_malloc(sizeof(*mf));
	mf->n_files = 0;
	mf->files = NULL;
	mf->n_file_infos = 0;
	mf->file_infos = NULL;
	mf->n_results = 0;
	mf->results = NULL;

	return mf;
}

static struct manifest *
read_manifest(const char *path, char **errmsg)
{
	bool success = false;
	struct manifest *mf = create_empty_manifest();
	struct decompressor *decompressor = NULL;
	struct decompr_state *decompr_state = NULL;
	*errmsg = NULL;

	FILE *f = fopen(path, "rb");
	if (!f) {
		*errmsg = x_strdup("No such manifest file");
		goto out;
	}

	uint8_t header_bytes[COMMON_HEADER_SIZE];
	if (fread(header_bytes, sizeof(header_bytes), 1, f) != 1) {
		*errmsg = format("Failed to read header from %s", path);
		goto out;
	}

	common_header_from_bytes(&mf->header, header_bytes);

	if (memcmp(mf->header.magic, MAGIC, sizeof(MAGIC)) != 0) {
		*errmsg = format(
			"Result file has bad magic value 0x%x%x%x%x",
			mf->header.magic[0],
			mf->header.magic[1],
			mf->header.magic[2],
			mf->header.magic[3]);
		goto out;
	}

	if (mf->header.version != MANIFEST_VERSION) {
		*errmsg = format(
			"Unknown manifest version (actual %u, expected %u)",
			mf->header.version,
			MANIFEST_VERSION);
		goto out;
	}

	if (!common_header_verify(&mf->header, fileno(f), "manifest", errmsg)) {
		goto out;
	}

	decompressor = decompressor_from_type(mf->header.compression_type);
	if (!decompressor) {
		*errmsg = format(
			"Unknown compression type: %u", mf->header.compression_type);
		goto out;
	}

	decompr_state = decompressor->init(f);
	if (!decompr_state) {
		*errmsg = x_strdup("Failed to initialize decompressor");
		goto out;
	}

	READ_UINT32(mf->n_files);
	mf->files = x_calloc(mf->n_files, sizeof(*mf->files));
	for (uint32_t i = 0; i < mf->n_files; i++) {
		READ_STR(mf->files[i].path, mf->files[i].path_len);
	}

	READ_UINT32(mf->n_file_infos);
	mf->file_infos = x_calloc(mf->n_file_infos, sizeof(*mf->file_infos));
	for (uint32_t i = 0; i < mf->n_file_infos; i++) {
		READ_UINT32(mf->file_infos[i].index);
		READ_BYTES(mf->file_infos[i].digest.bytes, DIGEST_SIZE);
		READ_UINT64(mf->file_infos[i].fsize);
		READ_INT64(mf->file_infos[i].mtime);
		READ_INT64(mf->file_infos[i].ctime);
	}

	READ_UINT32(mf->n_results);
	mf->results = x_calloc(mf->n_results, sizeof(*mf->results));
	for (uint32_t i = 0; i < mf->n_results; i++) {
		READ_UINT32(mf->results[i].n_file_info_indexes);
		mf->results[i].file_info_indexes =
			x_calloc(mf->results[i].n_file_info_indexes,
			         sizeof(*mf->results[i].file_info_indexes));
		for (uint32_t j = 0; j < mf->results[i].n_file_info_indexes; j++) {
			READ_UINT32(mf->results[i].file_info_indexes[j]);
		}
		READ_BYTES(mf->results[i].name.bytes, DIGEST_SIZE);
	}

	success = true;

out:
	if (decompressor && !decompressor->free(decompr_state)) {
		success = false;
	}
	if (f) {
		fclose(f);
	}
	if (!success) {
		if (!*errmsg) {
			*errmsg = x_strdup("Corrupt manifest file");
		}
		free_manifest(mf);
		mf = NULL;
	}
	return mf;
}

#define WRITE_BYTES(buf, length) \
	do { \
		if (!compressor->write(compr_state, buf, length)) { \
			goto error; \
		} \
	} while (false)

#define WRITE_UINT16(var) \
	do { \
		char buf_[2]; \
		BYTES_FROM_UINT16(buf_, (var)); \
		WRITE_BYTES(buf_, sizeof(buf_)); \
	} while (false)

#define WRITE_UINT32(var) \
	do { \
		char buf_[4]; \
		BYTES_FROM_UINT32(buf_, (var)); \
		WRITE_BYTES(buf_, sizeof(buf_)); \
	} while (false)

#define WRITE_INT64(var) \
	do { \
		char buf_[8]; \
		BYTES_FROM_INT64(buf_, (var)); \
		WRITE_BYTES(buf_, sizeof(buf_)); \
	} while (false)

#define WRITE_UINT64(var) \
	do { \
		char buf_[8]; \
		BYTES_FROM_UINT64(buf_, (var)); \
		WRITE_BYTES(buf_, sizeof(buf_)); \
	} while (false)

static bool
write_manifest(FILE *f, const struct manifest *mf)
{
	uint64_t content_size = COMMON_HEADER_SIZE;
	content_size += 4; // n_files
	for (size_t i = 0; i < mf->n_files; i++) {
		content_size += 2 + mf->files[i].path_len;
	}
	content_size += 4; // n_file_infos
	content_size += mf->n_file_infos * (4 + DIGEST_SIZE + 8 + 8 + 8);
	content_size += 4; // n_results
	for (size_t i = 0; i < mf->n_results; i++) {
		content_size += 4; // n_file_info_indexes
		content_size += mf->results[i].n_file_info_indexes * 4;
		content_size += DIGEST_SIZE;
	}

	struct common_header header;
	common_header_from_config(&header, MAGIC, MANIFEST_VERSION, content_size);
	uint8_t header_bytes[COMMON_HEADER_SIZE];
	common_header_to_bytes(&header, header_bytes);
	if (fwrite(header_bytes, sizeof(header_bytes), 1, f) != 1) {
		goto error;
	}

	struct compressor *compressor =
		compressor_from_type(header.compression_type);
	assert(compressor);
	struct compr_state *compr_state =
		compressor->init(f, header.compression_level);
	if (!compr_state) {
		cc_log("Failed to initialize compressor");
		goto error;
	}

	WRITE_UINT32(mf->n_files);
	for (uint32_t i = 0; i < mf->n_files; i++) {
		WRITE_UINT16(mf->files[i].path_len);
		WRITE_BYTES(mf->files[i].path, mf->files[i].path_len);
	}

	WRITE_UINT32(mf->n_file_infos);
	for (uint32_t i = 0; i < mf->n_file_infos; i++) {
		WRITE_UINT32(mf->file_infos[i].index);
		WRITE_BYTES(mf->file_infos[i].digest.bytes, DIGEST_SIZE);
		WRITE_UINT64(mf->file_infos[i].fsize);
		WRITE_INT64(mf->file_infos[i].mtime);
		WRITE_INT64(mf->file_infos[i].ctime);
	}

	WRITE_UINT32(mf->n_results);
	for (uint32_t i = 0; i < mf->n_results; i++) {
		WRITE_UINT32(mf->results[i].n_file_info_indexes);
		for (uint32_t j = 0; j < mf->results[i].n_file_info_indexes; j++) {
			WRITE_UINT32(mf->results[i].file_info_indexes[j]);
		}
		WRITE_BYTES(mf->results[i].name.bytes, DIGEST_SIZE);
	}

	return compressor->free(compr_state);

error:
	cc_log("Error writing to manifest file");
	return false;
}

static bool
verify_result(struct conf *conf, struct manifest *mf, struct result *result,
              struct hashtable *stated_files, struct hashtable *hashed_files)
{
	for (uint32_t i = 0; i < result->n_file_info_indexes; i++) {
		struct file_info *fi = &mf->file_infos[result->file_info_indexes[i]];
		char *path = mf->files[fi->index].path;
		struct file_stats *st = hashtable_search(stated_files, path);
		if (!st) {
			struct stat file_stat;
			if (x_stat(path, &file_stat) != 0) {
				return false;
			}
			st = x_malloc(sizeof(*st));
			st->size = file_stat.st_size;
			st->mtime = file_stat.st_mtime;
			st->ctime = file_stat.st_ctime;
			hashtable_insert(stated_files, x_strdup(path), st);
		}

		if (fi->fsize != st->size) {
			return false;
		}

		// Clang stores the mtime of the included files in the precompiled header,
		// and will error out if that header is later used without rebuilding.
		if ((guessed_compiler == GUESSED_CLANG
		     || guessed_compiler == GUESSED_UNKNOWN)
		    && output_is_precompiled_header
		    && fi->mtime != st->mtime) {
			cc_log("Precompiled header includes %s, which has a new mtime", path);
			return false;
		}

		if (conf->sloppiness & SLOPPY_FILE_STAT_MATCHES) {
			if (!(conf->sloppiness & SLOPPY_FILE_STAT_MATCHES_CTIME)) {
				if (fi->mtime == st->mtime && fi->ctime == st->ctime) {
					cc_log("mtime/ctime hit for %s", path);
					continue;
				} else {
					cc_log("mtime/ctime miss for %s", path);
				}
			} else {
				if (fi->mtime == st->mtime) {
					cc_log("mtime hit for %s", path);
					continue;
				} else {
					cc_log("mtime miss for %s", path);
				}
			}
		}

		struct digest *actual = hashtable_search(hashed_files, path);
		if (!actual) {
			struct hash *hash = hash_init();
			int ret = hash_source_code_file(conf, hash, path);
			if (ret & HASH_SOURCE_CODE_ERROR) {
				cc_log("Failed hashing %s", path);
				hash_free(hash);
				return false;
			}
			if (ret & HASH_SOURCE_CODE_FOUND_TIME) {
				hash_free(hash);
				return false;
			}

			actual = malloc(sizeof(*actual));
			hash_result_as_bytes(hash, actual);
			hashtable_insert(hashed_files, x_strdup(path), actual);
			hash_free(hash);
		}
		if (!digests_equal(&fi->digest, actual)) {
			return false;
		}
	}

	return true;
}

static struct hashtable *
create_file_index_map(struct file *files, uint32_t len)
{
	struct hashtable *h =
		create_hashtable(1000, hash_from_string, strings_equal);
	for (uint32_t i = 0; i < len; i++) {
		uint32_t *index = x_malloc(sizeof(*index));
		*index = i;
		hashtable_insert(h, x_strdup(files[i].path), index);
	}
	return h;
}

static struct hashtable *
create_file_info_index_map(struct file_info *infos, uint32_t len)
{
	struct hashtable *h =
		create_hashtable(1000, hash_from_file_info, file_infos_equal);
	for (uint32_t i = 0; i < len; i++) {
		struct file_info *fi = x_malloc(sizeof(*fi));
		*fi = infos[i];
		uint32_t *index = x_malloc(sizeof(*index));
		*index = i;
		hashtable_insert(h, fi, index);
	}
	return h;
}

static uint32_t
get_include_file_index(struct manifest *mf, char *path,
                       struct hashtable *mf_files)
{
	uint32_t *index = hashtable_search(mf_files, path);
	if (index) {
		return *index;
	}

	uint32_t n = mf->n_files;
	mf->files = x_realloc(mf->files, (n + 1) * sizeof(*mf->files));
	mf->n_files++;
	mf->files[n].path_len = strlen(path);
	mf->files[n].path = x_strdup(path);
	return n;
}

static uint32_t
get_file_info_index(struct manifest *mf,
                    char *path,
                    struct digest *digest,
                    struct hashtable *mf_files,
                    struct hashtable *mf_file_infos)
{
	struct file_info fi;
	fi.index = get_include_file_index(mf, path, mf_files);
	fi.digest = *digest;

	// file_stat.st_{m,c}time has a resolution of 1 second, so we can cache the
	// file's mtime and ctime only if they're at least one second older than
	// time_of_compilation.
	//
	// st->ctime may be 0, so we have to check time_of_compilation against
	// MAX(mtime, ctime).

	struct stat file_stat;
	if (stat(path, &file_stat) != -1) {
		if (time_of_compilation > MAX(file_stat.st_mtime, file_stat.st_ctime)) {
			fi.mtime = file_stat.st_mtime;
			fi.ctime = file_stat.st_ctime;
		} else {
			fi.mtime = -1;
			fi.ctime = -1;
		}
		fi.fsize = file_stat.st_size;
	} else {
		fi.mtime = -1;
		fi.ctime = -1;
		fi.fsize = 0;
	}

	uint32_t *fi_index = hashtable_search(mf_file_infos, &fi);
	if (fi_index) {
		return *fi_index;
	}

	uint32_t n = mf->n_file_infos;
	mf->file_infos = x_realloc(mf->file_infos, (n + 1) * sizeof(*mf->file_infos));
	mf->n_file_infos++;
	mf->file_infos[n] = fi;
	return n;
}

static void
add_file_info_indexes(uint32_t *indexes, uint32_t size,
                      struct manifest *mf, struct hashtable *included_files)
{
	if (size == 0) {
		return;
	}

	// path --> index
	struct hashtable *mf_files = create_file_index_map(mf->files, mf->n_files);
	// struct file_info --> index
	struct hashtable *mf_file_infos =
		create_file_info_index_map(mf->file_infos, mf->n_file_infos);
	struct hashtable_itr *iter = hashtable_iterator(included_files);
	uint32_t i = 0;
	do {
		char *path = hashtable_iterator_key(iter);
		struct digest *digest = hashtable_iterator_value(iter);
		indexes[i] = get_file_info_index(mf, path, digest, mf_files,
		                                 mf_file_infos);
		i++;
	} while (hashtable_iterator_advance(iter));
	assert(i == size);

	hashtable_destroy(mf_file_infos, 1);
	hashtable_destroy(mf_files, 1);
}

static void
add_result_entry(struct manifest *mf,
                 struct digest *result_digest,
                 struct hashtable *included_files)
{
	uint32_t n_results = mf->n_results;
	mf->results = x_realloc(mf->results, (n_results + 1) * sizeof(*mf->results));
	mf->n_results++;
	struct result *result = &mf->results[n_results];

	uint32_t n_fii = hashtable_count(included_files);
	result->n_file_info_indexes = n_fii;
	result->file_info_indexes =
		x_malloc(n_fii * sizeof(*result->file_info_indexes));
	add_file_info_indexes(result->file_info_indexes, n_fii, mf, included_files);
	result->name = *result_digest;
}

// Try to get the result name from a manifest file. Caller frees. Returns NULL
// on failure.
struct digest *
manifest_get(struct conf *conf, const char *manifest_path)
{
	char *errmsg;
	struct manifest *mf = read_manifest(manifest_path, &errmsg);
	if (!mf) {
		cc_log("%s", errmsg);
		return NULL;
	}

	// path --> struct digest
	struct hashtable *hashed_files =
		create_hashtable(1000, hash_from_string, strings_equal);
	// path --> struct file_stats
	struct hashtable *stated_files =
		create_hashtable(1000, hash_from_string, strings_equal);

	// Check newest result first since it's a bit more likely to match.
	struct digest *name = NULL;
	for (uint32_t i = mf->n_results; i > 0; i--) {
		if (verify_result(conf, mf, &mf->results[i - 1],
		                  stated_files, hashed_files)) {
			name = x_malloc(sizeof(*name));
			*name = mf->results[i - 1].name;
			goto out;
		}
	}

out:
	if (hashed_files) {
		hashtable_destroy(hashed_files, 1);
	}
	if (stated_files) {
		hashtable_destroy(stated_files, 1);
	}
	free_manifest(mf);
	if (name) {
		// Update modification timestamp to save files from LRU cleanup.
		update_mtime(manifest_path);
	}
	return name;
}

// Put the result name into a manifest file given a set of included files.
// Returns true on success, otherwise false.
bool
manifest_put(const char *manifest_path, struct digest *result_name,
             struct hashtable *included_files)
{
	// We don't bother to acquire a lock when writing the manifest to disk. A
	// race between two processes will only result in one lost entry, which is
	// not a big deal, and it's also very unlikely.

	char *errmsg;
	struct manifest *mf = read_manifest(manifest_path, &errmsg);
	if (!mf) {
		// New or corrupt file.
		mf = create_empty_manifest();
		free(errmsg); // Ignore.
	}

	if (mf->n_results > MAX_MANIFEST_ENTRIES) {
		// Normally, there shouldn't be many result entries in the manifest since
		// new entries are added only if an include file has changed but not the
		// source file, and you typically change source files more often than
		// header files. However, it's certainly possible to imagine cases where
		// the manifest will grow large (for instance, a generated header file that
		// changes for every build), and this must be taken care of since
		// processing an ever growing manifest eventually will take too much time.
		// A good way of solving this would be to maintain the result entries in
		// LRU order and discarding the old ones. An easy way is to throw away all
		// entries when there are too many. Let's do that for now.
		cc_log("More than %u entries in manifest file; discarding",
		       MAX_MANIFEST_ENTRIES);
		free_manifest(mf);
		mf = create_empty_manifest();
	} else if (mf->n_file_infos > MAX_MANIFEST_FILE_INFO_ENTRIES) {
		// Rarely, file_info entries can grow large in pathological cases where
		// many included files change, but the main file does not. This also puts
		// an upper bound on the number of file_info entries.
		cc_log("More than %u file_info entries in manifest file; discarding",
		       MAX_MANIFEST_FILE_INFO_ENTRIES);
		free_manifest(mf);
		mf = create_empty_manifest();
	}

	add_result_entry(mf, result_name, included_files);

	int ret = false;
	char *tmp_file = format("%s.tmp", manifest_path);
	int fd = create_tmp_fd(&tmp_file);
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		cc_log("Failed to fdopen %s", tmp_file);
		goto out;
	}
	if (write_manifest(f, mf)) {
		fclose(f);
		f = NULL;
		if (x_rename(tmp_file, manifest_path) == 0) {
			ret = true;
		} else {
			cc_log("Failed to rename %s to %s", tmp_file, manifest_path);
		}
	} else {
		cc_log("Failed to write manifest file %s", tmp_file);
	}

out:
	if (f) {
		fclose(f);
	}
	if (mf) {
		free_manifest(mf);
	}
	if (tmp_file) {
		free(tmp_file);
	}
	return ret;
}

bool
manifest_dump(const char *manifest_path, FILE *stream)
{
	char *errmsg;
	struct manifest *mf = read_manifest(manifest_path, &errmsg);
	if (!mf) {
		assert(errmsg);
		fprintf(stream, "Error: %s\n", errmsg);
		free(errmsg);
		return false;
	}

	common_header_dump(&mf->header, stream);

	fprintf(stream, "File paths (%u):\n", (unsigned)mf->n_files);
	for (unsigned i = 0; i < mf->n_files; ++i) {
		fprintf(stream, "  %u: %s\n", i, mf->files[i].path);
	}
	fprintf(stream, "File infos (%u):\n", (unsigned)mf->n_file_infos);
	for (unsigned i = 0; i < mf->n_file_infos; ++i) {
		char digest[DIGEST_STRING_BUFFER_SIZE];
		fprintf(stream, "  %u:\n", i);
		fprintf(stream, "    Path index: %u\n", mf->file_infos[i].index);
		digest_as_string(&mf->file_infos[i].digest, digest);
		fprintf(stream, "    Hash: %s\n", digest);
		fprintf(stream, "    File size: %" PRIu64 "\n", mf->file_infos[i].fsize);
		fprintf(stream, "    Mtime: %lld\n", (long long)mf->file_infos[i].mtime);
		fprintf(stream, "    Ctime: %lld\n", (long long)mf->file_infos[i].ctime);
	}
	fprintf(stream, "Results (%u):\n", (unsigned)mf->n_results);
	for (unsigned i = 0; i < mf->n_results; ++i) {
		char name[DIGEST_STRING_BUFFER_SIZE];
		fprintf(stream, "  %u:\n", i);
		fprintf(stream, "    File info indexes:");
		for (unsigned j = 0; j < mf->results[i].n_file_info_indexes; ++j) {
			fprintf(stream, " %u", mf->results[i].file_info_indexes[j]);
		}
		fprintf(stream, "\n");
		digest_as_string(&mf->results[i].name, name);
		fprintf(stream, "    Name: %s\n", name);
	}

	free_manifest(mf);
	return true;
}
