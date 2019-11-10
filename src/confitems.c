// Copyright (C) 2018-2019 Joel Rosdahl
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

#include "confitems.h"
#include "ccache.h"

static char *
format_string(const void *value)
{
	const char *const *str = (const char *const *)value;
	return x_strdup(*str);
}

bool
confitem_parse_bool(const char *str, void *result, char **errmsg)
{
	bool *value = (bool *)result;

	if (str_eq(str, "true")) {
		*value = true;
		return true;
	} else if (str_eq(str, "false")) {
		*value = false;
		return true;
	} else {
		*errmsg = format("not a boolean value: \"%s\"", str);
		return false;
	}
}

char *
confitem_format_bool(const void *value)
{
	const bool *b = (const bool *)value;
	return x_strdup(*b ? "true" : "false");
}

bool
confitem_parse_env_string(const char *str, void *result, char **errmsg)
{
	char **value = (char **)result;
	free(*value);
	*value = subst_env_in_string(str, errmsg);
	return *value != NULL;
}

char *
confitem_format_env_string(const void *value)
{
	return format_string(value);
}

bool
confitem_parse_double(const char *str, void *result, char **errmsg)
{
	double *value = (double *)result;
	errno = 0;
	char *endptr;
	double x = strtod(str, &endptr);
	if (errno == 0 && *str != '\0' && *endptr == '\0') {
		*value = x;
		return true;
	} else {
		*errmsg = format("invalid floating point: \"%s\"", str);
		return false;
	}
}

char *
confitem_format_double(const void *value)
{
	const double *x = (const double *)value;
	return format("%.1f", *x);
}

bool
confitem_parse_size(const char *str, void *result, char **errmsg)
{
	uint64_t *value = (uint64_t *)result;
	uint64_t size;
	if (parse_size_with_suffix(str, &size)) {
		*value = size;
		return true;
	} else {
		*errmsg = format("invalid size: \"%s\"", str);
		return false;
	}
}

char *
confitem_format_size(const void *value)
{
	const uint64_t *size = (const uint64_t *)value;
	return format_parsable_size_with_suffix(*size);
}

bool
confitem_parse_sloppiness(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	if (!str) {
		return *value;
	}

	char *p = x_strdup(str);
	char *q = p;
	char *word;
	char *saveptr = NULL;
	while ((word = strtok_r(q, ", ", &saveptr))) {
		if (str_eq(word, "file_macro")) {
			*value |= SLOPPY_FILE_MACRO;
		} else if (str_eq(word, "file_stat_matches")) {
			*value |= SLOPPY_FILE_STAT_MATCHES;
		} else if (str_eq(word, "file_stat_matches_ctime")) {
			*value |= SLOPPY_FILE_STAT_MATCHES_CTIME;
		} else if (str_eq(word, "include_file_ctime")) {
			*value |= SLOPPY_INCLUDE_FILE_CTIME;
		} else if (str_eq(word, "include_file_mtime")) {
			*value |= SLOPPY_INCLUDE_FILE_MTIME;
		} else if (str_eq(word, "system_headers")
		           || str_eq(word, "no_system_headers")) {
			*value |= SLOPPY_SYSTEM_HEADERS;
		} else if (str_eq(word, "pch_defines")) {
			*value |= SLOPPY_PCH_DEFINES;
		} else if (str_eq(word, "time_macros")) {
			*value |= SLOPPY_TIME_MACROS;
		} else if (str_eq(word, "clang_index_store")) {
			*value |= SLOPPY_CLANG_INDEX_STORE;
		} else if (str_eq(word, "locale")) {
			*value |= SLOPPY_LOCALE;
		} else {
			*errmsg = format("unknown sloppiness: \"%s\"", word);
			free(p);
			return false;
		}
		q = NULL;
	}
	free(p);
	return true;
}

char *
confitem_format_sloppiness(const void *value)
{
	const unsigned *sloppiness = (const unsigned *)value;
	char *s = x_strdup("");
	if (*sloppiness & SLOPPY_FILE_MACRO) {
		reformat(&s, "%sfile_macro, ", s);
	}
	if (*sloppiness & SLOPPY_INCLUDE_FILE_MTIME) {
		reformat(&s, "%sinclude_file_mtime, ", s);
	}
	if (*sloppiness & SLOPPY_INCLUDE_FILE_CTIME) {
		reformat(&s, "%sinclude_file_ctime, ", s);
	}
	if (*sloppiness & SLOPPY_TIME_MACROS) {
		reformat(&s, "%stime_macros, ", s);
	}
	if (*sloppiness & SLOPPY_PCH_DEFINES) {
		reformat(&s, "%spch_defines, ", s);
	}
	if (*sloppiness & SLOPPY_FILE_STAT_MATCHES) {
		reformat(&s, "%sfile_stat_matches, ", s);
	}
	if (*sloppiness & SLOPPY_FILE_STAT_MATCHES_CTIME) {
		reformat(&s, "%sfile_stat_matches_ctime, ", s);
	}
	if (*sloppiness & SLOPPY_SYSTEM_HEADERS) {
		reformat(&s, "%ssystem_headers, ", s);
	}
	if (*sloppiness & SLOPPY_CLANG_INDEX_STORE) {
		reformat(&s, "%sclang_index_store, ", s);
	}
	if (*sloppiness & SLOPPY_LOCALE) {
		reformat(&s, "%slocale, ", s);
	}
	if (*sloppiness) {
		// Strip last ", ".
		s[strlen(s) - 2] = '\0';
	}
	return s;
}

bool
confitem_parse_string(const char *str, void *result, char **errmsg)
{
	(void)errmsg;

	char **value = (char **)result;
	free(*value);
	*value = x_strdup(str);
	return true;
}

char *
confitem_format_string(const void *value)
{
	return format_string(value);
}

bool
confitem_parse_umask(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	if (str_eq(str, "")) {
		*value = UINT_MAX;
		return true;
	}

	errno = 0;
	char *endptr;
	*value = strtoul(str, &endptr, 8);
	if (errno == 0 && *str != '\0' && *endptr == '\0') {
		return true;
	} else {
		*errmsg = format("not an octal integer: \"%s\"", str);
		return false;
	}
}

char *
confitem_format_umask(const void *value)
{
	const unsigned *umask = (const unsigned *)value;
	if (*umask == UINT_MAX) {
		return x_strdup("");
	} else {
		return format("%03o", *umask);
	}
}

bool
confitem_parse_unsigned(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	errno = 0;
	char *endptr;
	long x = strtol(str, &endptr, 10);
	if (errno == 0 && x >= 0 && *str != '\0' && *endptr == '\0') {
		*value = x;
		return true;
	} else {
		*errmsg = format("invalid unsigned integer: \"%s\"", str);
		return false;
	}
}

char *
confitem_format_unsigned(const void *value)
{
	const unsigned *i = (const unsigned *)value;
	return format("%u", *i);
}

bool
confitem_verify_absolute_path(const void *value, char **errmsg)
{
	const char *const *path = (const char *const *)value;
	assert(*path);
	if (str_eq(*path, "")) {
		// The empty string means "disable" in this case.
		return true;
	} else if (is_absolute_path(*path)) {
		return true;
	} else {
		*errmsg = format("not an absolute path: \"%s\"", *path);
		return false;
	}
}

bool
confitem_verify_dir_levels(const void *value, char **errmsg)
{
	const unsigned *levels = (const unsigned *)value;
	assert(levels);
	if (*levels >= 1 && *levels <= 8) {
		return true;
	} else {
		*errmsg = format("cache directory levels must be between 1 and 8");
		return false;
	}
}
