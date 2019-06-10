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
#include "manifest.h"
#include "murmurhashneutral2.h"

#include <zlib.h>

// Sketchy specification of the manifest data format:
//
// <magic>         magic number                          (4 bytes: cCmF)
// <version>       file format version                   (1 byte unsigned int)
// <not_used>      not used                              (3 bytes)
// ----------------------------------------------------------------------------
// <n>             number of include file paths          (4 bytes unsigned int)
// <path_0>        include file path                     (NUL-terminated string,
// ...                                                    at most 1024 bytes)
// <path_n-1>
// ----------------------------------------------------------------------------
// <n>             number of include file entries        (4 bytes unsigned int)
// <index[0]>      include file path index               (4 bytes unsigned int)
// <digest[0]>     include file digest                   (DIGEST_SIZE bytes)
// <fsize[0]>      include file size                     (8 bytes unsigned int)
// <mtime[0]>      include file mtime                    (8 bytes signed int)
// <ctime[0]>      include file ctime                    (8 bytes signed int)
// ...
// <index[n-1]>
// <digest[n-1]>
// <fsize[n-1]>
// <mtime[n-1]>
// <ctime[n-1]>
// ----------------------------------------------------------------------------
// <n>             number of result entries              (4 bytes unsigned int)
// <m[0]>          number of include file entry indexes  (4 bytes unsigned int)
// <index[0][0]>   include file entry index              (4 bytes unsigned int)
// ...
// <index[0][m[0]-1]>
// <name[0]>       result name                           (DIGEST_SIZE bytes)
// ...

static const uint32_t MAGIC = 0x63436d46U; // cCmF
static const uint32_t MAX_MANIFEST_ENTRIES = 100;
static const uint32_t MAX_MANIFEST_FILE_INFO_ENTRIES = 10000;

#define ccache_static_assert(e) \
	do { enum { ccache_static_assert__ = 1/(e) }; } while (false)

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
	// Version of decoded file.
	uint8_t version;

	// Referenced include files.
	uint32_t n_files;
	char **files;

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
	return murmurhashneutral2(key, sizeof(struct file_info), 0);
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
		free(mf->files[i]);
	}
	free(mf->files);
	free(mf->file_infos);
	for (uint32_t i = 0; i < mf->n_results; i++) {
		free(mf->results[i].file_info_indexes);
	}
	free(mf->results);
	free(mf);
}

#define READ_BYTE(var) \
	do { \
		int ch_ = gzgetc(f); \
		if (ch_ == EOF) { \
			goto error; \
		} \
		(var) = ch_ & 0xFF; \
	} while (false)

#define READ_INT(size, var) \
	do { \
		uint64_t u_ = 0; \
		for (size_t i_ = 0; i_ < (size); i_++) { \
			int ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			u_ <<= 8; \
			u_ |= ch_ & 0xFF; \
		} \
		(var) = u_; \
	} while (false)

#define READ_STR(var) \
	do { \
		char buf_[1024]; \
		size_t i_; \
		for (i_ = 0; i_ < sizeof(buf_); i_++) { \
			int ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			buf_[i_] = ch_; \
			if (ch_ == '\0') { \
				break; \
			} \
		} \
		if (i_ == sizeof(buf_)) { \
			goto error; \
		} \
		(var) = x_strdup(buf_); \
	} while (false)

#define READ_BYTES(n, var) \
	do { \
		for (size_t i_ = 0; i_ < (n); i_++) { \
			int ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			(var)[i_] = ch_; \
		} \
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
read_manifest(gzFile f, char **errmsg)
{
	*errmsg = NULL;
	struct manifest *mf = create_empty_manifest();

	uint32_t magic;
	READ_INT(4, magic);
	if (magic != MAGIC) {
		*errmsg = format("Manifest file has bad magic number %u", magic);
		goto error;
	}

	READ_BYTE(mf->version);
	if (mf->version != MANIFEST_VERSION) {
		*errmsg = format(
			"Unknown manifest version (actual %u, expected %u)",
			mf->version,
			MANIFEST_VERSION);
		goto error;
	}

	char dummy[3]; // Legacy "hash size" + "reserved". TODO: Remove.
	READ_BYTES(3, dummy);
	(void)dummy;

	READ_INT(4, mf->n_files);
	mf->files = x_calloc(mf->n_files, sizeof(*mf->files));
	for (uint32_t i = 0; i < mf->n_files; i++) {
		READ_STR(mf->files[i]);
	}

	READ_INT(4, mf->n_file_infos);
	mf->file_infos = x_calloc(mf->n_file_infos, sizeof(*mf->file_infos));
	for (uint32_t i = 0; i < mf->n_file_infos; i++) {
		READ_INT(4, mf->file_infos[i].index);
		READ_BYTES(DIGEST_SIZE, mf->file_infos[i].digest.bytes);
		READ_INT(8, mf->file_infos[i].fsize);
		READ_INT(8, mf->file_infos[i].mtime);
		READ_INT(8, mf->file_infos[i].ctime);
	}

	READ_INT(4, mf->n_results);
	mf->results = x_calloc(mf->n_results, sizeof(*mf->results));
	for (uint32_t i = 0; i < mf->n_results; i++) {
		READ_INT(4, mf->results[i].n_file_info_indexes);
		mf->results[i].file_info_indexes =
			x_calloc(mf->results[i].n_file_info_indexes,
			         sizeof(*mf->results[i].file_info_indexes));
		for (uint32_t j = 0; j < mf->results[i].n_file_info_indexes; j++) {
			READ_INT(4, mf->results[i].file_info_indexes[j]);
		}
		READ_BYTES(DIGEST_SIZE, mf->results[i].name.bytes);
	}

	return mf;

error:
	if (!*errmsg) {
		*errmsg = x_strdup("Corrupt manifest file");
	}
	free_manifest(mf);
	return NULL;
}

#define WRITE_INT(size, var) \
	do { \
		uint64_t u_ = (var); \
		uint8_t ch_; \
		size_t i_; \
		for (i_ = 0; i_ < (size); i_++) { \
			ch_ = (u_ >> (8 * ((size) - i_ - 1))); \
			if (gzputc(f, ch_) == EOF) { \
				goto error; \
			} \
		} \
	} while (false)

#define WRITE_STR(var) \
	do { \
		if (gzputs(f, var) == EOF || gzputc(f, '\0') == EOF) { \
			goto error; \
		} \
	} while (false)

#define WRITE_BYTES(n, var) \
	do { \
		size_t i_; \
		for (i_ = 0; i_ < (n); i_++) { \
			if (gzputc(f, (var)[i_]) == EOF) { \
				goto error; \
			} \
		} \
	} while (false)

static int
write_manifest(gzFile f, const struct manifest *mf)
{
	WRITE_INT(4, MAGIC);
	WRITE_INT(1, MANIFEST_VERSION);
	WRITE_INT(1, 16); // Legacy hash size field. TODO: Remove.
	WRITE_INT(2, 0); // Legacy "reserved" field. TODO: Remove.

	WRITE_INT(4, mf->n_files);
	for (uint32_t i = 0; i < mf->n_files; i++) {
		WRITE_STR(mf->files[i]);
	}

	WRITE_INT(4, mf->n_file_infos);
	for (uint32_t i = 0; i < mf->n_file_infos; i++) {
		WRITE_INT(4, mf->file_infos[i].index);
		WRITE_BYTES(DIGEST_SIZE, mf->file_infos[i].digest.bytes);
		WRITE_INT(8, mf->file_infos[i].fsize);
		WRITE_INT(8, mf->file_infos[i].mtime);
		WRITE_INT(8, mf->file_infos[i].ctime);
	}

	WRITE_INT(4, mf->n_results);
	for (uint32_t i = 0; i < mf->n_results; i++) {
		WRITE_INT(4, mf->results[i].n_file_info_indexes);
		for (uint32_t j = 0; j < mf->results[i].n_file_info_indexes; j++) {
			WRITE_INT(4, mf->results[i].file_info_indexes[j]);
		}
		WRITE_BYTES(DIGEST_SIZE, mf->results[i].name.bytes);
	}

	return 1;

error:
	cc_log("Error writing to manifest file");
	return 0;
}

static bool
verify_result(struct conf *conf, struct manifest *mf, struct result *result,
              struct hashtable *stated_files, struct hashtable *hashed_files)
{
	for (uint32_t i = 0; i < result->n_file_info_indexes; i++) {
		struct file_info *fi = &mf->file_infos[result->file_info_indexes[i]];
		char *path = mf->files[fi->index];
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
create_string_index_map(char **strings, uint32_t len)
{
	struct hashtable *h =
		create_hashtable(1000, hash_from_string, strings_equal);
	for (uint32_t i = 0; i < len; i++) {
		uint32_t *index = x_malloc(sizeof(*index));
		*index = i;
		hashtable_insert(h, x_strdup(strings[i]), index);
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
	mf->files[n] = x_strdup(path);
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
	struct hashtable *mf_files =
		create_string_index_map(mf->files, mf->n_files);
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
	gzFile f = NULL;
	struct manifest *mf = NULL;
	struct hashtable *hashed_files = NULL; // path --> struct digest
	struct hashtable *stated_files = NULL; // path --> struct file_stats
	struct digest *name = NULL;

	int fd = open(manifest_path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		// Cache miss.
		cc_log("No such manifest file");
		goto out;
	}
	f = gzdopen(fd, "rb");
	if (!f) {
		close(fd);
		cc_log("Failed to gzdopen manifest file");
		goto out;
	}

	char *errmsg;
	mf = read_manifest(f, &errmsg);
	if (!mf) {
		cc_log("%s", errmsg);
		goto out;
	}

	hashed_files = create_hashtable(1000, hash_from_string, strings_equal);
	stated_files = create_hashtable(1000, hash_from_string, strings_equal);

	// Check newest result first since it's a bit more likely to match.
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
	if (f) {
		gzclose(f);
	}
	if (mf) {
		free_manifest(mf);
	}
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
	int ret = 0;
	gzFile f2 = NULL;
	struct manifest *mf = NULL;
	char *tmp_file = NULL;

	// We don't bother to acquire a lock when writing the manifest to disk. A
	// race between two processes will only result in one lost entry, which is
	// not a big deal, and it's also very unlikely.

	int fd1 = open(manifest_path, O_RDONLY | O_BINARY);
	if (fd1 == -1) {
		// New file.
		mf = create_empty_manifest();
	} else {
		gzFile f1 = gzdopen(fd1, "rb");
		if (!f1) {
			cc_log("Failed to gzdopen manifest file");
			close(fd1);
			goto out;
		}
		char *errmsg;
		mf = read_manifest(f1, &errmsg);
		gzclose(f1);
		if (!mf) {
			cc_log("%s", errmsg);
			free(errmsg);
			cc_log("Failed to read manifest file; deleting it");
			x_unlink(manifest_path);
			mf = create_empty_manifest();
		}
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

	tmp_file = format("%s.tmp", manifest_path);
	int fd2 = create_tmp_fd(&tmp_file);
	f2 = gzdopen(fd2, "wb");
	if (!f2) {
		cc_log("Failed to gzdopen %s", tmp_file);
		goto out;
	}

	add_result_entry(mf, result_name, included_files);
	if (write_manifest(f2, mf)) {
		gzclose(f2);
		f2 = NULL;
		if (x_rename(tmp_file, manifest_path) == 0) {
			ret = 1;
		} else {
			cc_log("Failed to rename %s to %s", tmp_file, manifest_path);
			goto out;
		}
	} else {
		cc_log("Failed to write manifest file");
		goto out;
	}

out:
	if (mf) {
		free_manifest(mf);
	}
	if (tmp_file) {
		free(tmp_file);
	}
	if (f2) {
		gzclose(f2);
	}
	return ret;
}

bool
manifest_dump(const char *manifest_path, FILE *stream)
{
	struct manifest *mf = NULL;
	gzFile f = NULL;
	bool ret = false;

	int fd = open(manifest_path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		fprintf(stderr, "No such manifest file: %s\n", manifest_path);
		goto out;
	}
	f = gzdopen(fd, "rb");
	if (!f) {
		fprintf(stderr, "Failed to dzopen manifest file\n");
		close(fd);
		goto out;
	}
	char *errmsg;
	mf = read_manifest(f, &errmsg);
	if (!mf) {
		fprintf(stderr, "%s\n", errmsg);
		free(errmsg);
		goto out;
	}

	fprintf(stream, "Magic: %c%c%c%c\n",
	        (MAGIC >> 24) & 0xFF,
	        (MAGIC >> 16) & 0xFF,
	        (MAGIC >> 8) & 0xFF,
	        MAGIC & 0xFF);
	fprintf(stream, "Version: %u\n", mf->version);
	fprintf(stream, "File paths (%u):\n", (unsigned)mf->n_files);
	for (unsigned i = 0; i < mf->n_files; ++i) {
		fprintf(stream, "  %u: %s\n", i, mf->files[i]);
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

	ret = true;

out:
	if (mf) {
		free_manifest(mf);
	}
	if (f) {
		gzclose(f);
	}
	return ret;
}
