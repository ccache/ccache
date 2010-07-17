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

/*
 * This file contains tests for the processing of compiler arguments.
 */

#include "ccache.h"
#include "framework.h"

TEST_SUITE(argument_processing)

TEST(dash_E_should_be_unsupported)
{
	struct args *orig = args_init_from_string("cc -c foo.c -E");
	struct args *preprocessed, *compiler;

	CHECK(!cc_process_args(orig, &preprocessed, &compiler));
	CHECK_UNS_EQ(1, stats_get_pending(STATS_UNSUPPORTED));

	args_free(orig);
}

TEST_SUITE_END
