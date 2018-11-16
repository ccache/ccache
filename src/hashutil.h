// Copyright (C) 2009-2018 Joel Rosdahl
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

#ifndef HASHUTIL_H
#define HASHUTIL_H

#include "conf.h"
#include "hash.h"
#include <inttypes.h>

struct file_hash
{
	uint8_t hash[16];
	uint32_t size;
};

unsigned hash_from_string(void *str);
unsigned hash_from_int(int i);
int strings_equal(void *str1, void *str2);
int file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2);

#define HASH_SOURCE_CODE_OK 0
#define HASH_SOURCE_CODE_ERROR 1
#define	HASH_SOURCE_CODE_FOUND_DATE 2
#define	HASH_SOURCE_CODE_FOUND_TIME 4

int check_for_temporal_macros(const char *str, size_t len);
int hash_source_code_string(
	struct conf *conf, struct hash *hash, const char *str, size_t len,
	const char *path);
int hash_source_code_file(
	struct conf *conf, struct hash *hash, const char *path);
bool hash_command_output(struct hash *hash, const char *command,
                         const char *compiler);
bool hash_multicommand_output(struct hash *hash, const char *command,
                              const char *compiler);

#endif
