// Copyright (C) 2009-2018 Joel Rosdahl
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
#include "result.h"

#include <zlib.h>

static const uint32_t MAGIC = 0x63436343U;

struct file {
	uint32_t suffix_len;
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

	f->suffix_len = strlen(suffix);
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

#define READ_FILE(size, path) \
	do { \
		FILE *f_ = fopen(path, "wb"); \
		long i_; \
		for (i_ = 0; i_ < (size); i_++) { \
			int ch_ = gzgetc(f); \
			if (ch_ == EOF) { \
				goto error; \
			} \
			if (fputc(ch_, f_) == EOF) { \
				goto error; \
			} \
		} \
		fclose(f_); \
	} while (false)


static struct filelist *
read_cache(gzFile f, struct filelist *l, bool copy)
{
	uint32_t magic;
	READ_INT(4, magic);
	if (magic != MAGIC) {
		cc_log("Cache file has bad magic number %u", magic);
		goto error;
	}

	uint8_t version;
	READ_BYTE(version);
	(void)version;

	uint8_t hash_size;
	READ_BYTE(hash_size);
	(void)hash_size;

	uint16_t reserved;
	READ_INT(2, reserved);
	(void)reserved;

	uint32_t n_files;
	READ_INT(4, n_files);

	for (uint32_t i = 0; i < n_files; i++) {
		uint32_t sufflen;
		READ_INT(4, sufflen);
		char *suffix;
		READ_STR(suffix);

		uint32_t filelen;
		READ_INT(4, filelen);

		cc_log("Reading file #%d: %s (%u)", i, suffix, filelen);

		bool found = false;
		if (copy) {
			for (uint32_t j = 0; j < l->n_files; j++) {
				if (sufflen == l->files[j].suffix_len &&
				str_eq(suffix, l->files[j].suffix)) {
					found = true;

					cc_log("Copying %s from cache", l->files[i].path);

					READ_FILE(filelen, l->files[j].path);
				}
			}
		} else {
			add_file_to_filelist(l, "", suffix);
			l->sizes[l->n_files-1] = filelen;
		}
		if (!found) {
			// Skip the data, if no match
			gzseek(f, filelen, SEEK_CUR);
		}

		free(suffix);
	}
	return l;

error:
	cc_log("Corrupt cache file");
	free_filelist(l);
	return NULL;
}

#define WRITE_BYTE(var) \
	do { \
		if (gzputc(f, var) == EOF) { \
			goto error; \
		} \
	} while (false)

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

#define WRITE_FILE(size, path) \
	do { \
		FILE *f_ = fopen(path, "rb"); \
		uint8_t ch_; \
		long i_; \
		for (i_ = 0; i_ < (size); i_++) { \
			ch_ = fgetc(f_); \
			if (gzputc(f, ch_) == EOF) { \
				goto error; \
			} \
		} \
		fclose(f_); \
	} while (false)

static int
write_cache(gzFile f, const struct filelist *l)
{
	WRITE_INT(4, MAGIC);

	WRITE_BYTE(RESULT_VERSION);
	WRITE_BYTE(16);
	WRITE_INT(2, 0);

	WRITE_INT(4, l->n_files);
	for (uint32_t i = 0; i < l->n_files; i++) {
		struct stat st;
		if (x_stat(l->files[i].path, &st) != 0) {
			return -1;
		}

		cc_log("Writing file #%d: %s (%ld)", i, l->files[i].suffix,
		       (long)st.st_size);

		WRITE_INT(4, l->files[i].suffix_len);
		WRITE_STR(l->files[i].suffix);

		cc_log("Copying %s to cache", l->files[i].path);

		WRITE_INT(4, st.st_size);
		WRITE_FILE(st.st_size, l->files[i].path);
	}

	return 1;

error:
	cc_log("Error writing to cache file");
	return 0;
}

bool cache_get(const char *cache_path, struct filelist *l)
{
	int ret = 0;
	gzFile f = NULL;

	int fd = open(cache_path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		// Cache miss.
		cc_log("No such cache file");
		goto out;
	}
	f = gzdopen(dup(fd), "rb");
	if (!f) {
		close(fd);
		cc_log("Failed to gzdopen cache file");
		goto out;
	}
	l = read_cache(f, l, true);
	if (!l) {
		cc_log("Error reading cache file");
		goto out;
	}
	ret = 1;
out:
	if (f) {
		gzclose(f);
	}
	return ret;
}

bool cache_put(const char *cache_path, struct filelist *l)
{
	int ret = 0;
	gzFile f2 = NULL;
	char *tmp_file = NULL;
	bool compress = true;

	tmp_file = format("%s.tmp", cache_path);
	int fd = create_tmp_fd(&tmp_file);
	f2 = gzdopen(fd, compress ? "wb" : "wbT");
	if (!f2) {
		cc_log("Failed to gzdopen %s", tmp_file);
		goto out;
	}

	if (write_cache(f2, l)) {
		gzclose(f2);
		f2 = NULL;
		if (x_rename(tmp_file, cache_path) == 0) {
			ret = 1;
		} else {
			cc_log("Failed to rename %s to %s", tmp_file, cache_path);
			goto out;
		}
	} else {
		cc_log("Failed to write cache file");
		goto out;
	}
out:
	if (tmp_file) {
		free(tmp_file);
	}
	if (f2) {
		gzclose(f2);
	}
	return ret;
}

bool
cache_dump(const char *cache_path, FILE *stream)
{
	struct filelist *l = create_empty_filelist();
	gzFile f = NULL;
	bool ret = false;

	int fd = open(cache_path, O_RDONLY | O_BINARY);
	if (fd == -1) {
		fprintf(stderr, "No such cache file: %s\n", cache_path);
		goto out;
	}
	f = gzdopen(fd, "rb");
	if (!f) {
		fprintf(stderr, "Failed to gzdopen cache file\n");
		close(fd);
		goto out;
	}
	l = read_cache(f, l, false);
	if (!l) {
		fprintf(stderr, "Error reading cache file\n");
		goto out;
	}

	fprintf(stream, "Magic: %c%c%c%c\n",
	        (MAGIC >> 24) & 0xFF,
	        (MAGIC >> 16) & 0xFF,
	        (MAGIC >> 8) & 0xFF,
	        MAGIC & 0xFF);
	fprintf(stream, "File paths (%u):\n", (unsigned)l->n_files);
	for (unsigned i = 0; i < l->n_files; ++i) {
		fprintf(stream, "  %u: %s (%s)\n", i, l->files[i].suffix,
		                format_human_readable_size(l->sizes[i]));
	}

	ret = true;

out:
	if (l) {
		free_filelist(l);
	}
	if (f) {
		gzclose(f);
	}
	return ret;
}
