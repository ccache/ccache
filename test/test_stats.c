/*
 * Copyright (C) 2010-2011 Joel Rosdahl
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

/*
 * This file contains tests for statistics handling.
 */

#include "ccache.h"
#include "counters.h"
#include "test/framework.h"
#include "test/util.h"

TEST_SUITE(stats)

TEST(forward_compatibility)
{
	unsigned i;
	FILE *f;
	struct counters *counters = counters_init(0);

	f = fopen("stats", "w");
	for (i = 0; i < 100; i++) {
		fprintf(f, "%u\n", i);
	}
	fclose(f);

	stats_read("stats", counters);
	CHECK_INT_EQ(100, counters->size);
	CHECK_INT_EQ(73, counters->data[73]);

	stats_write("stats", counters);
	CHECK_INT_EQ(100, counters->size);
	CHECK_INT_EQ(99, counters->data[99]);

	counters_free(counters);
}

TEST_SUITE_END
