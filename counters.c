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

/* A simple array of unsigned integers used for the statistics counters. */

#include "ccache.h"

/*
 * Allocate and initialize a struct counters. Data entries up to the size are
 * set to 0.
 */
struct counters *
counters_init(size_t initial_size)
{
	struct counters *c = x_malloc(sizeof(*c));
	c->data = NULL;
	c->size = 0;
	c->allocated = 0;
	counters_resize(c, initial_size);
	return c;
}

/*
 * Free a struct counters.
 */
void
counters_free(struct counters *c)
{
	free(c->data);
	free(c);
}

/*
 * Set a new size. New data entries are set to 0.
 */
void
counters_resize(struct counters *c, size_t new_size)
{
	if (new_size > c->size) {
		size_t i;
		bool realloc = false;

		while (c->allocated < new_size) {
			c->allocated += 32 + c->allocated;
			realloc = true;
		}
		if (realloc) {
			c->data = x_realloc(c->data, c->allocated * sizeof(c->data[0]));
		}
		for (i = c->size; i < new_size; i++) {
			c->data[i] = 0;
		}
	}

	c->size = new_size;
}
