// Copyright (C) 2011-2019 Joel Rosdahl and other contributors
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

struct conf {
	char *base_dir;
	char *cache_dir;
	unsigned cache_dir_levels;
	char *compiler;
	char *compiler_check;
	bool compression;
	int compression_level;
	char *cpp_extension;
	bool debug;
	bool depend_mode;
	bool direct_mode;
	bool disable;
	char *extra_files_to_hash;
	bool file_clone;
	bool hard_link;
	bool hash_dir;
	char *ignore_headers_in_manifest;
	bool keep_comments_cpp;
	double limit_multiple;
	char *log_file;
	unsigned max_files;
	uint64_t max_size;
	char *path;
	bool pch_external_checksum;
	char *prefix_command;
	char *prefix_command_cpp;
	bool read_only;
	bool read_only_direct;
	bool recache;
	bool run_second_cpp;
	unsigned sloppiness;
	bool stats;
	char *temporary_dir;
	unsigned umask;
	bool unify;

	const char **item_origins;
};

struct conf *conf_create(void);
void conf_free(struct conf *conf);
bool conf_read(struct conf *conf, const char *path, char **errmsg);
bool conf_update_from_environment(struct conf *conf, char **errmsg);
bool conf_print_value(struct conf *conf, const char *key,
                      FILE *file, char **errmsg);
bool conf_set_value_in_file(const char *path, const char *key,
                            const char *value, char **errmsg);
bool conf_print_items(struct conf *conf,
                      void (*printer)(const char *descr, const char *origin,
                                      void *context),
                      void *context);
