// Copyright (C) 1997-1998 Andrew Tridgell
// Copyright (C) 2009-2019 Joel Rosdahl and other contributors
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

#ifndef MDFOUR_H
#define MDFOUR_H

#include <stddef.h>
#include <inttypes.h>

struct mdfour {
	uint32_t A, B, C, D;
	size_t totalN;
	size_t tail_len;
	unsigned char tail[64];
};

void mdfour_begin(struct mdfour *md);
void mdfour_update(struct mdfour *md, const unsigned char *in, size_t n);
void mdfour_result(struct mdfour *md, unsigned char *out);

#endif
