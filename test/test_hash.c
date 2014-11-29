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
 * This file contains tests for functions in hash.c.
 */

#include "ccache.h"
#include "test/framework.h"

TEST_SUITE(hash)

TEST(test_vectors_from_rfc_1320_should_be_correct)
{
	HSTATE_T h;

	hash_start(&h);
	hash_string(&h, "");
	CHECK_STR_EQ_FREE2("99e9d85137db46ef0000000000000000-0", hash_result(&h));

	hash_start(&h);
	hash_string(&h, "a");
	CHECK_STR_EQ_FREE2("5b6e8ca9f1c44ed20000000000000000-1", hash_result(&h));

	hash_start(&h);
	hash_string(&h, "message digest");
	CHECK_STR_EQ_FREE2("beb3eefc28d76e060000000000000000-14", hash_result(&h));

	hash_start(&h);
	hash_string(&h, "12345678901234567890123456789012345678901234567890123456789012345678901234567890");
	CHECK_STR_EQ_FREE2("5d14ee197f474ae00000000000000000-80", hash_result(&h));
}

TEST(hash_result_should_be_idempotent)
{
	HSTATE_T h;

	hash_start(&h);
	hash_string(&h, "");
	CHECK_STR_EQ_FREE2("99e9d85137db46ef0000000000000000-0", hash_result(&h));
	CHECK_STR_EQ_FREE2("99e9d85137db46ef0000000000000000-0", hash_result(&h));
}

TEST_SUITE_END
