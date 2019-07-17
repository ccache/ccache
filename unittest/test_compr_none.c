// Copyright (C) 2019 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
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

#include "../src/compression.h"
#include "framework.h"
#include "util.h"

TEST_SUITE(compr_type_none)

TEST(small_roundtrip)
{
	const uint64_t expected_foobar_checksum = 0xa2aa05ed9085aaf9ULL;

	XXH64_state_t *checksum = XXH64_createState();
	XXH64_reset(checksum, 0);

	FILE *f = fopen("data.uncompressed", "w");
	struct compressor *compr_none = compressor_from_type(COMPR_TYPE_NONE);
	struct compr_state *c_state = compr_none->init(f, -1, checksum);
	CHECK(c_state);

	CHECK(compr_none->write(c_state, "foobar", 6));

	CHECK(compr_none->free(c_state));
	fclose(f);

	CHECK_INT_EQ(XXH64_digest(checksum), expected_foobar_checksum);

	XXH64_reset(checksum, 0);
	f = fopen("data.uncompressed", "r");
	struct decompressor *decompr_none = decompressor_from_type(COMPR_TYPE_NONE);
	struct decompr_state *d_state = decompr_none->init(f, checksum);
	CHECK(d_state);

	char buffer[4];
	CHECK(decompr_none->read(d_state, buffer, 4));
	CHECK(memcmp(buffer, "foob", 4) == 0);
	CHECK(decompr_none->read(d_state, buffer, 2));
	CHECK(memcmp(buffer, "ar", 2) == 0);

	// Nothing left to read.
	CHECK(!decompr_none->read(d_state, buffer, 1));

	// Error state is remembered.
	CHECK(!decompr_none->free(d_state));
	fclose(f);

	CHECK_INT_EQ(XXH64_digest(checksum), expected_foobar_checksum);

	XXH64_freeState(checksum);
}

TEST_SUITE_END
