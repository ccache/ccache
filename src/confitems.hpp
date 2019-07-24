// Copyright (C) 2018-2019 Joel Rosdahl and other contributors
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

#ifndef CONFITEMS_H
#define CONFITEMS_H

#include "system.hpp"

typedef bool (*conf_item_parser)(const char *str, void *result, char **errmsg);
typedef bool (*conf_item_verifier)(const void *value, char **errmsg);
typedef char *(*conf_item_formatter)(const void *value);

struct conf_item {
	const char *name;
	size_t number;
	size_t offset;
	conf_item_parser parser;
	conf_item_formatter formatter;
	conf_item_verifier verifier;
};

bool confitem_parse_bool(const char *str, void *result, char **errmsg);
char *confitem_format_bool(const void *value);

bool confitem_parse_env_string(const char *str, void *result, char **errmsg);
char *confitem_format_env_string(const void *value);

bool confitem_parse_double(const char *str, void *result, char **errmsg);
char *confitem_format_double(const void *value);

bool confitem_parse_size(const char *str, void *result, char **errmsg);
char *confitem_format_size(const void *value);

bool confitem_parse_sloppiness(const char *str, void *result, char **errmsg);
char *confitem_format_sloppiness(const void *value);

bool confitem_parse_string(const char *str, void *result, char **errmsg);
char *confitem_format_string(const void *value);

bool confitem_parse_umask(const char *str, void *result, char **errmsg);
char *confitem_format_umask(const void *value);

bool confitem_parse_int(const char *str, void *result, char **errmsg);
char *confitem_format_int(const void *value);

bool confitem_parse_unsigned(const char *str, void *result, char **errmsg);
char *confitem_format_unsigned(const void *value);

bool confitem_verify_absolute_path(const void *value, char **errmsg);
bool confitem_verify_compression_level(const void *value, char **errmsg);
bool confitem_verify_dir_levels(const void *value, char **errmsg);

const struct conf_item *confitems_get(const char *str, size_t len);
size_t confitems_count(void);

#endif
