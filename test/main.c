/* Mode: -*-c-*- */
/*
 * Copyright (C) 2010 Joel Rosdahl
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

#include "test/framework.h"
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#else
#include "getopt_long.h"
#endif

#define SUITE(name) unsigned suite_ ## name(unsigned);
#include "test/suites.h"
#undef SUITE

const char USAGE_TEXT[] =
  "Usage:\n"
  "    test [options]\n"
  "\n"
  "Options:\n"
  "    -h, --help      print this help text\n"
  "    -v, --verbose   enable verbose logging of tests\n";

int
main(int argc, char **argv)
{
	suite_fn suites[] = {
#define SUITE(name) &suite_ ## name,
#include "test/suites.h"
#undef SUITE
		NULL
	};
	static const struct option options[] = {
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{NULL, 0, NULL, 0}
	};
	int verbose = 0;
	int c;
	char *testdir, *dir_before;
	int result;

#ifdef _WIN32
	putenv("CCACHE_DETECT_SHEBANG=1");
#endif

	while ((c = getopt_long(argc, argv, "hv", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			fprintf(stdout, USAGE_TEXT);
			return 0;

		case 'v':
			verbose = 1;
			break;

		default:
			fprintf(stderr, USAGE_TEXT);
			return 1;
		}
	}

	if (getenv("RUN_FROM_BUILD_FARM")) {
		verbose = 1;
	}

	testdir = format("testdir.%d", (int)getpid());
	cct_create_fresh_dir(testdir);
	dir_before = gnu_getcwd();
	cct_chdir(testdir);
	result = cct_run(suites, verbose);
	if (result == 0) {
		cct_chdir(dir_before);
		cct_wipe(testdir);
	}
	free(testdir);
	free(dir_before);
	return result;
}
