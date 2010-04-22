/*
 * Copyright (C) 2002-2006 Andrew Tridgell
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/*
 * When "max files" or "max cache size" is reached, one of the 16 cache
 * subdirectories is cleaned up. When doing so, files are deleted (in LRU
 * order) until the levels are below LIMIT_MULTIPLE.
 */
#define LIMIT_MULTIPLE 0.8

static struct files {
	char *fname;
	time_t mtime;
	size_t size; /* In KiB. */
} **files;
static unsigned allocated;
static unsigned num_files;
static size_t total_object_size;
static size_t total_object_files;
static size_t object_size_threshold;
static size_t object_files_threshold;

static int is_object_file(const char *fname)
{
	int i;
	int len;

	len = strlen(fname);
	if (len < 2) {
		return 0;
	}

	/* ccache 3.0 and later: */
	if (len >= 2 && fname[len - 2] == '.' && fname[len - 1] == 'o') {
		return 1;
	}

	/* ccache 2.4 and earlier: */
	for (i = len - 1; i >= 0; i--) {
		if (fname[i] == '.') {
			return 0;
		} else if (fname[i] == '-') {
			return 1;
		}
	}
	return 1;
}

/* file comparison function to try to delete the oldest files first */
static int files_compare(struct files **f1, struct files **f2)
{
	if ((*f2)->mtime == (*f1)->mtime) {
		return strcmp((*f2)->fname, (*f1)->fname);
	}
	if ((*f2)->mtime > (*f1)->mtime) {
		return -1;
	}
	return 1;
}

/* this builds the list of files in the cache */
static void traverse_fn(const char *fname, struct stat *st)
{
	char *p;

	if (!S_ISREG(st->st_mode)) return;

	p = basename(fname);
	if (strcmp(p, "stats") == 0) {
		free(p);
		return;
	}

	if (strstr(fname, ".tmp.") != NULL) {
		/* delete any tmp files older than 1 hour */
		if (st->st_mtime + 3600 < time(NULL)) {
			unlink(fname);
			free(p);
			return;
		}
	}


	free(p);

	if (num_files == allocated) {
		allocated = 10000 + num_files*2;
		files = (struct files **)x_realloc(
			files, sizeof(struct files *)*allocated);
	}

	files[num_files] = (struct files *)x_malloc(sizeof(struct files));
	files[num_files]->fname = x_strdup(fname);
	files[num_files]->mtime = st->st_mtime;
	files[num_files]->size = file_size(st) / 1024;
	if (is_object_file(fname)) {
		total_object_files += 1;
		total_object_size += files[num_files]->size;
	}
	num_files++;
}

static void delete_file(const char *path, size_t size)
{
	if (unlink(path) == 0) {
		if (is_object_file(path)) {
			total_object_files -= 1;
			total_object_size -= size;
		}
	} else if (errno != ENOENT) {
		fprintf(stderr, "ccache: failed to unlink %s (%s)\n",
			path, strerror(errno));
	}
}

static void delete_sibling_file(const char *base, const char *extension)
{
	struct stat st;
	char *path;

	x_asprintf(&path, "%s%s", base, extension);
	if (lstat(path, &st) == 0) {
		delete_file(path, file_size(&st) / 1024);
	} else if (errno != ENOENT) {
		fprintf(stderr, "ccache: failed to stat %s (%s)\n",
			path, strerror(errno));
	}
	free(path);
}

/* sort the files we've found and delete the oldest ones until we are
   below the thresholds */
static void sort_and_clean(void)
{
	unsigned i;
	const char *ext;

	if (num_files > 1) {
		/* sort in ascending data order */
		qsort(files, num_files, sizeof(struct files *),
		      (COMPAR_FN_T)files_compare);
	}

	/* delete enough files to bring us below the threshold */
	for (i = 0; i < num_files; i++) {
		if ((object_size_threshold == 0
		     || total_object_size < object_size_threshold)
		    && (object_files_threshold == 0
			|| (num_files-i) < object_files_threshold)) {
			break;
		}

		delete_file(files[i]->fname, files[i]->size);

		ext = get_extension(files[i]->fname);
		if (strcmp(ext, ".manifest") == 0) {
			/* Nothing more to do. */
		} else {
			/*
			 * Make sure that any sibling files are deleted as
			 * well.
			 */
			char *base = remove_extension(files[i]->fname);
			if (strcmp(ext, "") != 0) {
				/* Object file from ccache 2.4. */
				delete_sibling_file(base, "");
			}
			if (strcmp(ext, ".d") != 0) {
				delete_sibling_file(base, ".d");
			}
			if (strcmp(ext, ".o") != 0) {
				delete_sibling_file(base, ".o");
			}
			if (strcmp(ext, ".stderr") != 0) {
				delete_sibling_file(base, ".stderr");
			}
			free(base);
		}
	}
}

/* cleanup in one cache subdir */
void cleanup_dir(const char *dir, size_t maxfiles, size_t maxsize)
{
	unsigned i;

	cc_log("Cleaning up cache directory %s", dir);

	object_size_threshold = maxsize * LIMIT_MULTIPLE;
	object_files_threshold = maxfiles * LIMIT_MULTIPLE;

	num_files = 0;
	total_object_files = 0;
	total_object_size = 0;

	/* build a list of files */
	traverse(dir, traverse_fn);

	/* clean the cache */
	sort_and_clean();

	stats_set_sizes(dir, total_object_files, total_object_size);

	/* free it up */
	for (i = 0; i < num_files; i++) {
		free(files[i]->fname);
		free(files[i]);
		files[i] = NULL;
	}
	if (files) {
		free(files);
	}
	allocated = 0;
	files = NULL;

	num_files = 0;
	total_object_files = 0;
	total_object_size = 0;
}

/* cleanup in all cache subdirs */
void cleanup_all(const char *dir)
{
	unsigned counters[STATS_END];
	char *dname, *sfile;
	int i;

	for (i = 0; i <= 0xF; i++) {
		x_asprintf(&dname, "%s/%1x", dir, i);
		x_asprintf(&sfile, "%s/%1x/stats", dir, i);

		memset(counters, 0, sizeof(counters));
		stats_read(sfile, counters);

		cleanup_dir(dname,
			    counters[STATS_MAXFILES],
			    counters[STATS_MAXSIZE]);
		free(dname);
		free(sfile);
	}
}

/* traverse function for wiping files */
static void wipe_fn(const char *fname, struct stat *st)
{
	char *p;

	if (!S_ISREG(st->st_mode)) return;

	p = basename(fname);
	if (strcmp(p, "stats") == 0) {
		free(p);
		return;
	}
	free(p);

	unlink(fname);
}

/* wipe all cached files in all subdirs */
void wipe_all(const char *dir)
{
	char *dname;
	int i;

	for (i = 0; i <= 0xF; i++) {
		x_asprintf(&dname, "%s/%1x", dir, i);
		traverse(dir, wipe_fn);
		free(dname);
	}

	/* and fix the counters */
	cleanup_all(dir);
}
