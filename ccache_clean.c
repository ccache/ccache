/*
  a re-implementation of the compilercache scripts in C

  The idea is based on the shell-script compilercache by Erik Thiele <erikyyy@erikyyy.de>

   Copyright (C) Andrew Tridgell 2002
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "ccache.h"

#define BLOCK_SIZE 1024

char *cache_logfile = NULL;

static struct files {
	char *fname;
	time_t mtime;
	size_t size;
} **files;
static unsigned allocated;
static unsigned num_files;
static size_t total_size;
static size_t size_threshold = 1024*1024; /* 1G default */

static int files_compare(struct files *f1, struct files *f2)
{
	return f2->mtime - f1->mtime;
}

/* this builds the list of files in the cache */
static void traverse_fn(const char *fname, struct stat *st)
{
	if (!S_ISREG(st->st_mode)) return;

	if (num_files == allocated) {
		allocated = 10000 + num_files*2;
		files = x_realloc(files, sizeof(struct files *)*allocated);
	}

	files[num_files] = x_malloc(sizeof(struct files *));
	files[num_files]->fname = x_strdup(fname);
	files[num_files]->mtime = st->st_mtime;
	/* we deliberately overestimate by up to 1 block */
	files[num_files]->size = 1 + (st->st_size/BLOCK_SIZE);
	total_size += files[num_files]->size;
	num_files++;
}

static void sort_and_clean(void)
{
	unsigned i;

	if (num_files > 1) {
		/* sort in ascending data order */
		qsort(files, num_files, sizeof(struct files *), files_compare);
	}

	/* delete enough files to bring us below the threshold */
	for (i=0;i<num_files && total_size >= size_threshold;i++) {
		if (unlink(files[i]->fname) != 0 && errno != ENOENT) {
			fprintf(stderr, "unlink %s - %s\n", 
				files[i]->fname, strerror(errno));
			continue;
		}
		total_size -= files[i]->size;
	}

	printf("cleaned %d of %d files (cache is now %.1f MByte)\n", 
	       i, num_files, total_size/1024.0);
}


int main(int argc, char *argv[])
{
	char *cache_dir;
	
	cache_dir = getenv("CCACHE_DIR");
	if (!cache_dir) {
		x_asprintf(&cache_dir, "%s/.ccache", getenv("HOME"));
	}

	cache_logfile = getenv("CCACHE_LOGFILE");

	/* make sure the cache dir exists */
	if (create_dir(cache_dir) != 0) {
		fprintf(stderr,"ccache: failed to create %s (%s)\n", 
			cache_dir, strerror(errno));
		exit(1);
	}

	/* work out what size cache they want */
	if (argc > 1) {
		char *s = argv[1];
		char m;
		size_threshold = atoi(s);
		m = s[strlen(s)-1];
		switch (m) {
		case 'G':
		case 'g':
			size_threshold *= 1024*1024;
			break;
		case 'M':
		case 'm':
			size_threshold *= 1024;
			break;
		case 'K':
		case 'k':
			size_threshold *= 1;
			break;
		}
	}

	/* build a list of files */
	traverse(cache_dir, traverse_fn);

	/* clean the cache */
	sort_and_clean();

	return 0;
}
