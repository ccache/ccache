/*
 * Copyright (C) 2009-2010 Joel Rosdahl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ccache.h"
#include "hashtable_itr.h"
#include "hashutil.h"
#include "manifest.h"
#include "murmurhashneutral2.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

/*
 * Sketchy specification of the manifest disk format:
 *
 * <magic>         magic number                        (4 bytes)
 * <version>       file format version                 (1 byte unsigned int)
 * <hash_size>     size of the hash fields (in bytes)  (1 byte unsigned int)
 * <reserved>      reserved for future use             (2 bytes)
 * ----------------------------------------------------------------------------
 * <n>             number of include file paths        (4 bytes unsigned int)
 * <path_0>        path to include file                (NUL-terminated string,
 * ...                                                  at most 1024 bytes)
 * <path_n-1>
 * ----------------------------------------------------------------------------
 * <n>             number of include file hash entries (4 bytes unsigned int)
 * <index[0]>      index of include file path          (4 bytes unsigned int)
 * <hash[0]>       hash of include file                (<hash_size> bytes)
 * <size[0]>       size of include file                (4 bytes unsigned int)
 * ...
 * <hash[n-1]>
 * <size[n-1]>
 * <index[n-1]>
 * ----------------------------------------------------------------------------
 * <n>             number of object name entries       (4 bytes unsigned int)
 * <m[0]>          number of include file hash indexes (4 bytes unsigned int)
 * <index[0][0]>   include file hash index             (4 bytes unsigned int)
 * ...
 * <index[0][m[0]-1]>
 * <hash[0]>       hash part of object name            (<hash_size> bytes)
 * <size[0]>       size part of object name            (4 bytes unsigned int)
 * ...
 * <m[n-1]>        number of include file hash indexes
 * <index[n-1][0]> include file hash index
 * ...
 * <index[n-1][m[n-1]]>
 * <hash[n-1]>
 * <size[n-1]>
 */

static const uint32_t MAGIC = 0x63436d46U;
static const uint8_t  VERSION = 0;
static const uint32_t MAX_MANIFEST_ENTRIES = 100;

#define static_assert(e) do { enum { static_assert__ = 1/(e) }; } while (0)

struct file_info {
	/* Index to n_files. */
	uint32_t index;
	/* Hash of referenced file. */
	uint8_t hash[16];
	/* Size of referenced file. */
	uint32_t size;
};

struct object {
	/* Number of entries in file_info_indexes. */
	uint32_t n_file_info_indexes;
	/* Indexes to file_infos. */
	uint32_t *file_info_indexes;
	/* Hash of the object itself. */
	struct file_hash hash;
};

struct manifest {
	/* Size of hash fields (in bytes). */
	uint8_t hash_size;

	/* Referenced include files. */
	uint32_t n_files;
	char **files;

	/* Information about referenced include files. */
	uint32_t n_file_infos;
	struct file_info *file_infos;

	/* Object names plus references to include file hashes. */
	uint32_t n_objects;
	struct object *objects;
};

static unsigned int
hash_from_file_info(void *key)
{
	static_assert(sizeof(struct file_info) == 24); /* No padding. */
	return murmurhashneutral2(key, sizeof(struct file_info), 0);
}

static int
file_infos_equal(void *key1, void *key2)
{
	struct file_info *fi1 = (struct file_info *)key1;
	struct file_info *fi2 = (struct file_info *)key2;
	return fi1->index == fi2->index
	       && memcmp(fi1->hash, fi2->hash, 16) == 0
	       && fi1->size == fi2->size;
}

static void
free_manifest(struct manifest *mf)
{
	uint16_t i;
	for (i = 0; i < mf->n_files; i++) {
		free(mf->files[i]);
	}
	free(mf->files);
	free(mf->file_infos);
	for (i = 0; i < mf->n_objects; i++) {
		free(mf->objects[i].file_info_indexes);
	}
	free(mf->objects);
}

#define READ_INT(size, var) \
	do { \
		int ch_; \
		size_t i_; \
		(var) = 0; \
		for (i_ = 0; i_ < (size); i_++) { \
			ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			(var) <<= 8; \
			(var) |= ch_ & 0xFF; \
		} \
	} while (0)

#define READ_STR(var) \
	do { \
		char buf_[1024]; \
		size_t i_; \
		int ch_; \
		for (i_ = 0; i_ < sizeof(buf_); i_++) { \
			ch_ = gzgetc(f); \
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
	} while (0)

#define READ_BYTES(n, var) \
	do { \
		size_t i_; \
		int ch_; \
		for (i_ = 0; i_ < (n); i_++) { \
			ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			(var)[i_] = ch_; \
		} \
	} while (0)

static struct manifest *
create_empty_manifest(void)
{
	struct manifest *mf;

	mf = x_malloc(sizeof(*mf));
	mf->hash_size = 16;
	mf->n_files = 0;
	mf->files = NULL;
	mf->n_file_infos = 0;
	mf->file_infos = NULL;
	mf->n_objects = 0;
	mf->objects = NULL;

	return mf;
}

static struct manifest *
read_manifest(gzFile f)
{
	struct manifest *mf;
	uint16_t i, j;
	uint32_t magic;
	uint8_t version;
	uint16_t dummy;

	mf = create_empty_manifest();

	READ_INT(4, magic);
	if (magic != MAGIC) {
		cc_log("Manifest file has bad magic number %u", magic);
		free_manifest(mf);
		return NULL;
	}
	READ_INT(1, version);
	if (version != VERSION) {
		cc_log("Manifest file has unknown version %u", version);
		free_manifest(mf);
		return NULL;
	}

	READ_INT(1, mf->hash_size);
	if (mf->hash_size != 16) {
		/* Temporary measure until we support different hash algorithms. */
		cc_log("Manifest file has unsupported hash size %u", mf->hash_size);
		free_manifest(mf);
		return NULL;
	}

	READ_INT(2, dummy);

	READ_INT(4, mf->n_files);
	mf->files = x_calloc(mf->n_files, sizeof(*mf->files));
	for (i = 0; i < mf->n_files; i++) {
		READ_STR(mf->files[i]);
	}

	READ_INT(4, mf->n_file_infos);
	mf->file_infos = x_calloc(mf->n_file_infos, sizeof(*mf->file_infos));
	for (i = 0; i < mf->n_file_infos; i++) {
		READ_INT(4, mf->file_infos[i].index);
		READ_BYTES(mf->hash_size, mf->file_infos[i].hash);
		READ_INT(4, mf->file_infos[i].size);
	}

	READ_INT(4, mf->n_objects);
	mf->objects = x_calloc(mf->n_objects, sizeof(*mf->objects));
	for (i = 0; i < mf->n_objects; i++) {
		READ_INT(4, mf->objects[i].n_file_info_indexes);
		mf->objects[i].file_info_indexes =
			x_calloc(mf->objects[i].n_file_info_indexes,
			         sizeof(*mf->objects[i].file_info_indexes));
		for (j = 0; j < mf->objects[i].n_file_info_indexes; j++) {
			READ_INT(4, mf->objects[i].file_info_indexes[j]);
		}
		READ_BYTES(mf->hash_size, mf->objects[i].hash.hash);
		READ_INT(4, mf->objects[i].hash.size);
	}

	return mf;

error:
	cc_log("Corrupt manifest file");
	free_manifest(mf);
	return NULL;
}

#define WRITE_INT(size, var) \
	do { \
		char ch_; \
		size_t i_; \
		for (i_ = 0; i_ < (size); i_++) { \
			ch_ = ((var) >> (8 * ((size) - i_ - 1))); \
			if (gzputc(f, ch_) == EOF) { \
				goto error; \
			} \
		} \
	} while (0)

#define WRITE_STR(var) \
	do { \
		if (gzputs(f, var) == EOF || gzputc(f, '\0') == EOF) { \
			goto error; \
		} \
	} while (0)

#define WRITE_BYTES(n, var) \
	do { \
		size_t i_; \
		for (i_ = 0; i_ < (n); i_++) { \
			if (gzputc(f, (var)[i_]) == EOF) { \
				goto error; \
			} \
		} \
	} while (0)

static int
write_manifest(gzFile f, const struct manifest *mf)
{
	uint16_t i, j;

	WRITE_INT(4, MAGIC);
	WRITE_INT(1, VERSION);
	WRITE_INT(1, 16);
	WRITE_INT(2, 0);

	WRITE_INT(4, mf->n_files);
	for (i = 0; i < mf->n_files; i++) {
		WRITE_STR(mf->files[i]);
	}

	WRITE_INT(4, mf->n_file_infos);
	for (i = 0; i < mf->n_file_infos; i++) {
		WRITE_INT(4, mf->file_infos[i].index);
		WRITE_BYTES(mf->hash_size, mf->file_infos[i].hash);
		WRITE_INT(4, mf->file_infos[i].size);
	}

	WRITE_INT(4, mf->n_objects);
	for (i = 0; i < mf->n_objects; i++) {
		WRITE_INT(4, mf->objects[i].n_file_info_indexes);
		for (j = 0; j < mf->objects[i].n_file_info_indexes; j++) {
			WRITE_INT(4, mf->objects[i].file_info_indexes[j]);
		}
		WRITE_BYTES(mf->hash_size, mf->objects[i].hash.hash);
		WRITE_INT(4, mf->objects[i].hash.size);
	}

	return 1;

error:
	cc_log("Error writing to manifest file");
	return 0;
}

static int
verify_object(struct manifest *mf, struct object *obj,
              struct hashtable *hashed_files)
{
	uint32_t i;
	struct file_info *fi;
	struct file_hash *actual;
	struct mdfour hash;
	int result;

	for (i = 0; i < obj->n_file_info_indexes; i++) {
		fi = &mf->file_infos[obj->file_info_indexes[i]];
		actual = hashtable_search(hashed_files, mf->files[fi->index]);
		if (!actual) {
			actual = x_malloc(sizeof(*actual));
			hash_start(&hash);
			result = hash_source_code_file(&hash, mf->files[fi->index]);
			if (result & HASH_SOURCE_CODE_ERROR) {
				cc_log("Failed hashing %s", mf->files[fi->index]);
				free(actual);
				return 0;
			}
			if (result & HASH_SOURCE_CODE_FOUND_TIME) {
				free(actual);
				return 0;
			}
			hash_result_as_bytes(&hash, actual->hash);
			actual->size = hash.totalN;
			hashtable_insert(hashed_files, x_strdup(mf->files[fi->index]), actual);
		}
		if (memcmp(fi->hash, actual->hash, mf->hash_size) != 0
		    || fi->size != actual->size) {
			return 0;
		}
	}

	return 1;
}

static struct hashtable *
create_string_index_map(char **strings, uint32_t len)
{
	uint32_t i;
	struct hashtable *h;
	uint32_t *index;

	h = create_hashtable(1000, hash_from_string, strings_equal);
	for (i = 0; i < len; i++) {
		index = x_malloc(sizeof(*index));
		*index = i;
		hashtable_insert(h, x_strdup(strings[i]), index);
	}
	return h;
}

static struct hashtable *
create_file_info_index_map(struct file_info *infos, uint32_t len)
{
	uint32_t i;
	struct hashtable *h;
	struct file_info *fi;
	uint32_t *index;

	h = create_hashtable(1000, hash_from_file_info, file_infos_equal);
	for (i = 0; i < len; i++) {
		fi = x_malloc(sizeof(*fi));
		*fi = infos[i];
		index = x_malloc(sizeof(*index));
		*index = i;
		hashtable_insert(h, fi, index);
	}
	return h;
}

static uint32_t
get_include_file_index(struct manifest *mf, char *path,
                       struct hashtable *mf_files)
{
	uint32_t *index;
	uint32_t n;

	index = hashtable_search(mf_files, path);
	if (index) {
		return *index;
	}

	n = mf->n_files;
	mf->files = x_realloc(mf->files, (n + 1) * sizeof(*mf->files));
	mf->n_files++;
	mf->files[n] = x_strdup(path);

	return n;
}

static uint32_t
get_file_hash_index(struct manifest *mf,
                    char *path,
                    struct file_hash *file_hash,
                    struct hashtable *mf_files,
                    struct hashtable *mf_file_infos)
{
	struct file_info fi;
	uint32_t *fi_index;
	uint32_t n;

	fi.index = get_include_file_index(mf, path, mf_files);
	memcpy(fi.hash, file_hash->hash, sizeof(fi.hash));
	fi.size = file_hash->size;

	fi_index = hashtable_search(mf_file_infos, &fi);
	if (fi_index) {
		return *fi_index;
	}

	n = mf->n_file_infos;
	mf->file_infos = x_realloc(mf->file_infos, (n + 1) * sizeof(*mf->file_infos));
	mf->n_file_infos++;
	mf->file_infos[n] = fi;

	return n;
}

static void
add_file_info_indexes(uint32_t *indexes, uint32_t size,
                      struct manifest *mf, struct hashtable *included_files)
{
	struct hashtable_itr *iter;
	uint32_t i;
	char *path;
	struct file_hash *file_hash;
	struct hashtable *mf_files; /* path --> index */
	struct hashtable *mf_file_infos; /* struct file_info --> index */

	if (size == 0) {
		return;
	}

	mf_files = create_string_index_map(mf->files, mf->n_files);
	mf_file_infos = create_file_info_index_map(mf->file_infos, mf->n_file_infos);
	iter = hashtable_iterator(included_files);
	i = 0;
	do {
		path = hashtable_iterator_key(iter);
		file_hash = hashtable_iterator_value(iter);
		indexes[i] = get_file_hash_index(mf, path, file_hash, mf_files,
		                                 mf_file_infos);
		i++;
	} while (hashtable_iterator_advance(iter));
	assert(i == size);

	hashtable_destroy(mf_file_infos, 1);
	hashtable_destroy(mf_files, 1);
}

static void
add_object_entry(struct manifest *mf,
                 struct file_hash *object_hash,
                 struct hashtable *included_files)
{
	struct object *obj;
	uint32_t n;

	n = mf->n_objects;
	mf->objects = x_realloc(mf->objects, (n + 1) * sizeof(*mf->objects));
	mf->n_objects++;
	obj = &mf->objects[n];

	n = hashtable_count(included_files);
	obj->n_file_info_indexes = n;
	obj->file_info_indexes = x_malloc(n * sizeof(*obj->file_info_indexes));
	add_file_info_indexes(obj->file_info_indexes, n, mf, included_files);
	memcpy(obj->hash.hash, object_hash->hash, mf->hash_size);
	obj->hash.size = object_hash->size;
}

/*
 * Try to get the object hash from a manifest file. Caller frees. Returns NULL
 * on failure.
 */
struct file_hash *
manifest_get(const char *manifest_path)
{
	int fd;
	gzFile f = NULL;
	struct manifest *mf = NULL;
	struct hashtable *hashed_files = NULL; /* path --> struct file_hash */
	uint32_t i;
	struct file_hash *fh = NULL;

	fd = open(manifest_path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		/* Cache miss. */
		cc_log("No such manifest file");
		goto out;
	}
	f = gzdopen(fd, "rb");
	if (!f) {
		cc_log("Failed to gzdopen manifest file");
		goto out;
	}
	mf = read_manifest(f);
	if (!mf) {
		cc_log("Error reading manifest file");
		goto out;
	}

	hashed_files = create_hashtable(1000, hash_from_string, strings_equal);

	/* Check newest object first since it's a bit more likely to match. */
	for (i = mf->n_objects; i > 0; i--) {
		if (verify_object(mf, &mf->objects[i - 1], hashed_files)) {
			fh = x_malloc(sizeof(*fh));
			*fh = mf->objects[i - 1].hash;
			goto out;
		}
	}

out:
	if (hashed_files) {
		hashtable_destroy(hashed_files, 1);
	}
	if (f) {
		gzclose(f);
	}
	if (mf) {
		free_manifest(mf);
	}
	return fh;
}

/*
 * Put the object name into a manifest file given a set of included files.
 * Returns 1 on success, otherwise 0.
 */
int
manifest_put(const char *manifest_path, struct file_hash *object_hash,
             struct hashtable *included_files)
{
	int ret = 0;
	int fd1;
	int fd2;
	struct stat st;
	gzFile f1 = NULL;
	gzFile f2 = NULL;
	struct manifest *mf = NULL;
	char *tmp_file = NULL;

	/*
	 * We don't bother to acquire a lock when writing the manifest to disk. A
	 * race between two processes will only result in one lost entry, which is
	 * not a big deal, and it's also very unlikely.
	 */

	fd1 = safe_open(manifest_path);
	if (fd1 == -1) {
		cc_log("Failed to open manifest file");
		goto out;
	}
	if (fstat(fd1, &st) != 0) {
		cc_log("Failed to stat manifest file");
		close(fd1);
		goto out;
	}
	if (st.st_size == 0) {
		/* New file. */
		mf = create_empty_manifest();
	} else {
		f1 = gzdopen(fd1, "rb");
		if (!f1) {
			cc_log("Failed to gzdopen manifest file");
			close(fd1);
			goto out;
		}
		mf = read_manifest(f1);
		if (!mf) {
			cc_log("Failed to read manifest file");
			gzclose(f1);
			goto out;
		}
	}

	if (f1) {
		gzclose(f1);
	} else {
		close(fd1);
	}

	if (mf->n_objects > MAX_MANIFEST_ENTRIES) {
		/*
		 * Normally, there shouldn't be many object entries in the manifest since
		 * new entries are added only if an include file has changed but not the
		 * source file, and you typically change source files more often than
		 * header files. However, it's certainly possible to imagine cases where
		 * the manifest will grow large (for instance, a generated header file that
		 * changes for every build), and this must be taken care of since
		 * processing an ever growing manifest eventually will take too much time.
		 * A good way of solving this would be to maintain the object entries in
		 * LRU order and discarding the old ones. An easy way is to throw away all
		 * entries when there are too many. Let's do that for now.
		 */
		cc_log("More than %u entries in manifest file; discarding",
		       MAX_MANIFEST_ENTRIES);
		free_manifest(mf);
		mf = create_empty_manifest();
	}

	tmp_file = format("%s.tmp.%s", manifest_path, tmp_string());
	fd2 = safe_open(tmp_file);
	if (fd2 == -1) {
		cc_log("Failed to open %s", tmp_file);
		goto out;
	}
	f2 = gzdopen(fd2, "wb");
	if (!f2) {
		cc_log("Failed to gzdopen %s", tmp_file);
		goto out;
	}

	add_object_entry(mf, object_hash, included_files);
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
