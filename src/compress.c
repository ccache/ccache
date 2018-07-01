// Copyright (C) 2002-2006 Andrew Tridgell
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

static unsigned num_files;
static unsigned comp_files;

static uint64_t cache_size;
static uint64_t real_size;

// This measures the size of files in the cache.
static void
measure_fn(const char *fname, struct stat *st)
{
	if (!S_ISREG(st->st_mode)) {
		return;
	}

	char *p = basename(fname);
	if (str_eq(p, "stats")) {
		return;
	}

	if (str_startswith(p, ".nfs")) {
		// Ignore temporary NFS files that may be left for open but deleted files.
		return;
	}

	if (strstr(p, "CACHEDIR.TAG")) {
		return;
	}

	cache_size += st->st_size;
	num_files++;
	if (file_is_compressed(fname)) {
		real_size += uncompressed_size(fname);
		comp_files++;
	} else {
		real_size += st->st_size;
	}
}

// Process up all cache subdirectories.
void compress_stats(struct conf *conf)
{
	num_files = 0;
	comp_files = 0;
	cache_size = 0;
	real_size = 0;

	for (int i = 0; i <= 0xF; i++) {
		char *dname = format("%s/%1x", conf->cache_dir, i);
		traverse(dname, measure_fn);
		free(dname);
	}

	char *cache_str = format_human_readable_size(cache_size);
	printf("Compressed size: %s, %.0f files\n",
	       cache_str, (double)comp_files);
	free(cache_str);
	char *real_str = format_human_readable_size(real_size);
	printf("Uncompressed size: %s, %.0f files\n",
	       real_str, (double)num_files);
	free(real_str);

	double percent = real_size > 0 ? (100.0 * comp_files) / num_files : 0.0;
	printf("Compressed files: %.2f %%\n", percent);
	double ratio = cache_size > 0 ? ((double) real_size) / cache_size : 0.0;
	double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;
	printf("Compression ratio: %.2f %% (%.1fx)\n", savings, ratio);
}

