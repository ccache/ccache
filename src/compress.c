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
#include "manifest.h"
#include "result.h"

static bool
get_content_size(
	const char *path, const char *magic, uint8_t version, size_t *size)
{
	char *errmsg;
	FILE *f = fopen(path, "rb");
	if (!f) {
		cc_log("Failed to open %s for reading: %s", path, strerror(errno));
		return false;
	}
	struct common_header header;
	bool success = common_header_initialize_for_reading(
		&header,
		f,
		magic,
		version,
		NULL,
		NULL,
		NULL,
		&errmsg);
	fclose(f);
	if (success) {
		*size = header.content_size;
	}

	return success;
}

static uint64_t on_disk_size;
static uint64_t compr_size;
static uint64_t compr_orig_size;
static uint64_t incompr_size;

// This measures the size of files in the cache.
static void
measure_fn(const char *fname, struct stat *st)
{
	if (!S_ISREG(st->st_mode)) {
		return;
	}

	char *p = basename(fname);
	if (str_eq(p, "stats")) {
		goto out;
	}

	if (str_startswith(p, ".nfs")) {
		// Ignore temporary NFS files that may be left for open but deleted files.
		goto out;
	}

	if (strstr(p, ".tmp.")) {
		// Ignore tmp files since they are transient.
		goto out;
	}

	if (strstr(p, "CACHEDIR.TAG")) {
		goto out;
	}

	on_disk_size += file_size(st);

	size_t content_size = 0;
	const char *file_ext = get_extension(p);
	bool is_compressible = false;
	if (str_eq(file_ext, ".manifest")) {
		is_compressible = get_content_size(fname, MANIFEST_MAGIC, MANIFEST_VERSION, &content_size);
	} else if (str_eq(file_ext, ".result")) {
		is_compressible = get_content_size(fname, RESULT_MAGIC, RESULT_VERSION, &content_size);
	}

	if (is_compressible) {
		compr_size += st->st_size;
		compr_orig_size += content_size;
	} else {
		incompr_size += st->st_size;
	}

out:
	free(p);
}

// Process up all cache subdirectories.
void compress_stats(struct conf *conf)
{
	on_disk_size = 0;
	compr_size = 0;
	compr_orig_size = 0;
	incompr_size = 0;

	for (int i = 0; i <= 0xF; i++) {
		char *dname = format("%s/%1x", conf->cache_dir, i);
		traverse(dname, measure_fn);
		free(dname);
	}

	double ratio = compr_size > 0 ? ((double) compr_orig_size) / compr_size : 0.0;
	double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;

	char *on_disk_size_str = format_human_readable_size(on_disk_size);
	char *cache_size_str = format_human_readable_size(compr_size + incompr_size);
	char *compr_size_str = format_human_readable_size(compr_size);
	char *compr_orig_size_str = format_human_readable_size(compr_orig_size);
	char *incompr_size_str = format_human_readable_size(incompr_size);

	printf("Total data:            %8s (%s disk blocks)\n",
	       cache_size_str, on_disk_size_str);
	printf("Compressible data:     %8s (%.1f%% of original size)\n",
	       compr_size_str, 100.0 - savings);
	printf("  - Original size:     %8s\n", compr_orig_size_str);
	printf("  - Compression ratio: %5.3f x  (%.1f%% space savings)\n",
	       ratio, savings);
	printf("Incompressible data:   %8s\n", incompr_size_str);

	free(incompr_size_str);
	free(compr_orig_size_str);
	free(compr_size_str);
	free(cache_size_str);
	free(on_disk_size_str);
}
