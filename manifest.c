/*
 * Copyright (C) 2009-2013 Joel Rosdahl
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
 * <mtime[0]>      mtime of include file               (8 bytes signed int)
 * <ctime[0]>      ctime of include file               (8 bytes signed int)
 * ...
 * <index[n-1]>
 * <hash[n-1]>
 * <size[n-1]>
 * <mtime[n-1]>
 * <ctime[n-1]>
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
static const uint32_t MAX_MANIFEST_ENTRIES = 100;
static const uint32_t MAX_MANIFEST_FILE_INFO_ENTRIES = 10000;

#define ccache_static_assert(e) \
	do { enum { ccache_static_assert__ = 1/(e) }; } while (false)

struct file_info {
	/* Index to n_files. */
	uint32_t index;
	/* Hash of referenced file. */
	uint8_t hash[16];
	/* Size of referenced file. */
	uint32_t size;
	/* mtime of referenced file. */
	int64_t mtime;
	/* ctime of referenced file. */
	int64_t ctime;
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
	/* Version of decoded file. */
	uint8_t version;

	/* Reserved for future use. */
	uint16_t reserved;

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

struct file_stats {
	uint32_t size;
	int64_t mtime;
	int64_t ctime;
};

static unsigned int
hash_from_file_info(void *key)
{
	ccache_static_assert(sizeof(struct file_info) == 40); /* No padding. */
	return murmurhashneutral2(key, sizeof(struct file_info), 0);
}

static int
file_infos_equal(void *key1, void *key2)
{
	struct file_info *fi1 = (struct file_info *)key1;
	struct file_info *fi2 = (struct file_info *)key2;
	return fi1->index == fi2->index
	       && memcmp(fi1->hash, fi2->hash, 16) == 0
	       && fi1->size == fi2->size
	       && fi1->mtime == fi2->mtime
	       && fi1->ctime == fi2->ctime;
}

static void
free_manifest(struct manifest *mf)
{
	uint32_t i;
	for (i = 0; i < mf->n_files; i++) {
		free(mf->files[i]);
	}
	free(mf->files);
	free(mf->file_infos);
	for (i = 0; i < mf->n_objects; i++) {
		free(mf->objects[i].file_info_indexes);
	}
	free(mf->objects);
	free(mf);
}

#define READ_BYTE(var) \
	do { \
		int ch_; \
		ch_ = gzgetc(f); \
		if (ch_ == EOF) { \
			goto error; \
		} \
		(var) = ch_ & 0xFF; \
	} while (false)

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
	} while (false)

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
	} while (false)

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
	} while (false)

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
	uint32_t i, j;
	uint32_t magic;

	mf = create_empty_manifest();

	READ_INT(4, magic);
	if (magic != MAGIC) {
		cc_log("Manifest file has bad magic number %u", magic);
		free_manifest(mf);
		return NULL;
	}
	READ_BYTE(mf->version);
	if (mf->version != MANIFEST_VERSION) {
		cc_log("Manifest file has unknown version %u", mf->version);
		free_manifest(mf);
		return NULL;
	}

	READ_BYTE(mf->hash_size);
	if (mf->hash_size != 16) {
		/* Temporary measure until we support different hash algorithms. */
		cc_log("Manifest file has unsupported hash size %u", mf->hash_size);
		free_manifest(mf);
		return NULL;
	}

	READ_INT(2, mf->reserved);

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
		READ_INT(8, mf->file_infos[i].mtime);
		READ_INT(8, mf->file_infos[i].ctime);
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
		uint8_t ch_; \
		size_t i_; \
		for (i_ = 0; i_ < (size); i_++) { \
			ch_ = ((var) >> (8 * ((size) - i_ - 1))); \
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
	uint32_t i, j;

	WRITE_INT(4, MAGIC);
	WRITE_INT(1, MANIFEST_VERSION);
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
		WRITE_INT(8, mf->file_infos[i].mtime);
		WRITE_INT(8, mf->file_infos[i].ctime);
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
verify_object(struct conf *conf, struct manifest *mf, struct object *obj,
              struct hashtable *stated_files, struct hashtable *hashed_files)
{
	uint32_t i;
	struct file_info *fi;
	struct file_hash *actual;
	struct file_stats *st;
	struct mdfour hash;
	int result;
	char *path;

	for (i = 0; i < obj->n_file_info_indexes; i++) {
		fi = &mf->file_infos[obj->file_info_indexes[i]];
		path = mf->files[fi->index];
		st = hashtable_search(hashed_files, path);
		if (!st) {
			struct stat file_stat;
			if (stat(path, &file_stat) == -1) {
				cc_log("Failed to stat include file %s: %s", path, strerror(errno));
				return 0;
			}
			st = x_malloc(sizeof(*st));
			st->size = file_stat.st_size;
			st->mtime = file_stat.st_mtime;
			st->ctime = file_stat.st_ctime;
			hashtable_insert(stated_files, x_strdup(path), st);
		}

		if (conf->sloppiness & SLOPPY_FILE_STAT_MATCHES) {
			/*
			 * st->ctime is sometimes 0, so we can't check that both st->ctime and
			 * st->mtime are greater than time_of_compilation. But it's sufficient to
			 * check that either is.
			 */
			if (fi->size == st->size
			    && fi->mtime == st->mtime
			    && fi->ctime == st->ctime
			    && MAX(st->mtime, st->ctime) >= time_of_compilation) {
				cc_log("size/mtime/ctime hit for %s", path);
				continue;
			}
			else {
				cc_log("size/mtime/ctime miss for %s", path);
			}
		}

		actual = hashtable_search(hashed_files, path);
		if (!actual) {
			actual = x_malloc(sizeof(*actual));
			hash_start(&hash);
			result = hash_source_code_file(conf, &hash, path);
			if (result & HASH_SOURCE_CODE_ERROR) {
				cc_log("Failed hashing %s", path);
				free(actual);
				return 0;
			}
			if (result & HASH_SOURCE_CODE_FOUND_TIME) {
				free(actual);
				return 0;
			}
			hash_result_as_bytes(&hash, actual->hash);
			actual->size = hash.totalN;
			hashtable_insert(hashed_files, x_strdup(path), actual);
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
	struct stat file_stat;

	fi.index = get_include_file_index(mf, path, mf_files);
	memcpy(fi.hash, file_hash->hash, sizeof(fi.hash));
	fi.size = file_hash->size;

	/*
	 * file_stat.st_{m,c}time has a resolution of 1 second, so we can cache the
	 * file's mtime and ctime only if they're at least one second older than
	 * time_of_compilation.
	 *
	 * st->ctime may be 0, so we have to check time_of_compilation against
	 * MAX(mtime, ctime).
	 */

	if (stat(path, &file_stat) != -1
	    && time_of_compilation > MAX(file_stat.st_mtime, file_stat.st_ctime)) {
		fi.mtime = file_stat.st_mtime;
		fi.ctime = file_stat.st_ctime;
	}
	else {
		fi.mtime = -1;
		fi.ctime = -1;
	}

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
manifest_get(struct conf *conf, const char *manifest_path)
{
	int fd;
	gzFile f = NULL;
	struct manifest *mf = NULL;
	struct hashtable *hashed_files = NULL; /* path --> struct file_hash */
	struct hashtable *stated_files = NULL; /* path --> struct file_stats */
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
		close(fd);
		cc_log("Failed to gzdopen manifest file");
		goto out;
	}
	mf = read_manifest(f);
	if (!mf) {
		cc_log("Error reading manifest file");
		goto out;
	}

	hashed_files = create_hashtable(1000, hash_from_string, strings_equal);
	stated_files = create_hashtable(1000, hash_from_string, strings_equal);

	/* Check newest object first since it's a bit more likely to match. */
	for (i = mf->n_objects; i > 0; i--) {
		if (verify_object(conf, mf, &mf->objects[i - 1],
		                  stated_files, hashed_files)) {
			fh = x_malloc(sizeof(*fh));
			*fh = mf->objects[i - 1].hash;
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
	return fh;
}

/*
 * Put the object name into a manifest file given a set of included files.
 * Returns true on success, otherwise false.
 */
bool
manifest_put(const char *manifest_path, struct file_hash *object_hash,
             struct hashtable *included_files)
{
	int ret = 0;
	int fd1;
	int fd2;
	gzFile f2 = NULL;
	struct manifest *mf = NULL;
	char *tmp_file = NULL;

	/*
	 * We don't bother to acquire a lock when writing the manifest to disk. A
	 * race between two processes will only result in one lost entry, which is
	 * not a big deal, and it's also very unlikely.
	 */

	fd1 = open(manifest_path, O_RDONLY | O_BINARY);
	if (fd1 == -1) {
		/* New file. */
		mf = create_empty_manifest();
	} else {
		gzFile f1 = gzdopen(fd1, "rb");
		if (!f1) {
			cc_log("Failed to gzdopen manifest file");
			close(fd1);
			goto out;
		}
		mf = read_manifest(f1);
		gzclose(f1);
		if (!mf) {
			cc_log("Failed to read manifest file; deleting it");
			x_unlink(manifest_path);
			mf = create_empty_manifest();
		}
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
	} else if (mf->n_file_infos > MAX_MANIFEST_FILE_INFO_ENTRIES) {
		/* Rarely, file_info entries can grow large in pathological cases where
		 * many included files change, but the main file does not. This also puts
		 * an upper bound on the number of file_info entries.
		 */
		cc_log("More than %u file_info entries in manifest file; discarding",
		       MAX_MANIFEST_FILE_INFO_ENTRIES);
		free_manifest(mf);
		mf = create_empty_manifest();
	}

	tmp_file = format("%s.tmp.%s", manifest_path, tmp_string());
	fd2 = safe_create_wronly(tmp_file);
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

bool
manifest_dump(const char *manifest_path, FILE *stream)
{
	struct manifest *mf = NULL;
	int fd;
	gzFile f = NULL;
	bool ret = false;
	unsigned i, j;

	fd = open(manifest_path, O_RDONLY | O_BINARY);
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
	mf = read_manifest(f);
	if (!mf) {
		fprintf(stderr, "Error reading manifest file\n");
		goto out;
	}

	fprintf(stream, "Magic: %c%c%c%c\n",
	        (MAGIC >> 24) & 0xFF,
	        (MAGIC >> 16) & 0xFF,
	        (MAGIC >> 8) & 0xFF,
	        MAGIC & 0xFF);
	fprintf(stream, "Version: %u\n", mf->version);
	fprintf(stream, "Hash size: %u\n", (unsigned)mf->hash_size);
	fprintf(stream, "Reserved field: %u\n", (unsigned)mf->reserved);
	fprintf(stream, "File paths (%u):\n", (unsigned)mf->n_files);
	for (i = 0; i < mf->n_files; ++i) {
		fprintf(stream, "  %u: %s\n", i, mf->files[i]);
	}
	fprintf(stream, "File infos (%u):\n", (unsigned)mf->n_file_infos);
	for (i = 0; i < mf->n_file_infos; ++i) {
		char *hash;
		fprintf(stream, "  %u:\n", i);
		fprintf(stream, "    Path index: %u\n", mf->file_infos[i].index);
		hash = format_hash_as_string(mf->file_infos[i].hash, -1);
		fprintf(stream, "    Hash: %s\n", hash);
		free(hash);
		fprintf(stream, "    Size: %u\n", mf->file_infos[i].size);
		fprintf(stream, "    Mtime: %lld\n", (long long)mf->file_infos[i].mtime);
		fprintf(stream, "    Ctime: %lld\n", (long long)mf->file_infos[i].ctime);
	}
	fprintf(stream, "Results (%u):\n", (unsigned)mf->n_objects);
	for (i = 0; i < mf->n_objects; ++i) {
		char *hash;
		fprintf(stream, "  %u:\n", i);
		fprintf(stream, "    File hash indexes:");
		for (j = 0; j < mf->objects[i].n_file_info_indexes; ++j) {
			fprintf(stream, " %u", mf->objects[i].file_info_indexes[j]);
		}
		fprintf(stream, "\n");
		hash = format_hash_as_string(mf->objects[i].hash.hash, -1);
		fprintf(stream, "    Hash: %s\n", hash);
		free(hash);
		fprintf(stream, "    Size: %u\n", (unsigned)mf->objects[i].hash.size);
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
