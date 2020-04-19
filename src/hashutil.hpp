// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#pragma once

#include "system.hpp"

#include "hash.hpp"

#include <inttypes.h>

class Config;
class Context;

unsigned hash_from_int(int i);

#define HASH_SOURCE_CODE_OK 0
#define HASH_SOURCE_CODE_ERROR 1
#define HASH_SOURCE_CODE_FOUND_DATE 2
#define HASH_SOURCE_CODE_FOUND_TIME 4
#define HASH_SOURCE_CODE_FOUND_TIMESTAMP 8

int check_for_temporal_macros(const char* str, size_t len);
int hash_source_code_string(const Config& config,
                            struct hash* hash,
                            const char* str,
                            size_t len,
                            const char* path);
int hash_source_code_file(const Config& config,
                          struct hash* hash,
                          const char* path,
                          size_t size_hint = 0);
bool hash_command_output(struct hash* hash,
                         const char* command,
                         const char* compiler);
bool hash_multicommand_output(struct hash* hash,
                              const char* command,
                              const char* compiler);
