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

#ifndef RESULT_H
#define RESULT_H

#include "conf.h"

extern const char RESULT_MAGIC[4];
#define RESULT_VERSION 1
#define RESULT_STDERR_NAME "<stderr>"

struct result_files;

struct result_files *result_files_init(void);
void result_files_add(
	struct result_files *c, const char *path, const char *suffix);
void result_files_free(struct result_files *c);

bool result_get(const char *path, struct result_files *list);
bool result_put(const char *path, struct result_files *list);
bool result_dump(const char *path, FILE *stream);

#endif
