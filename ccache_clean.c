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

static time_t threshold;
char *cache_logfile = NULL;

static void clean_fn(const char *fname)
{
	struct stat st;
	if (stat(fname, &st) != 0 || !S_ISREG(st.st_mode)) return;

	if (st.st_mtime >= threshold) return;

	if (unlink(fname) != 0) {
		cc_log("unlink %s - %s\n", fname, strerror(errno));
		return;
	}
	cc_log("cleaned %s\n", fname);
}


int main(int argc, char *argv[])
{
	char *cache_dir;
	int num_days = 7;

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

	if (argc > 1) {
		num_days = atoi(argv[1]);
	}

	threshold = time(NULL) - num_days*24*60*60;

	traverse(cache_dir, clean_fn);

	return 0;
}
