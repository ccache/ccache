// Copyright (C) 2011-2018 Joel Rosdahl
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

#include "conf.h"
#include "ccache.h"

typedef bool (*conf_item_parser)(const char *str, void *result, char **errmsg);
typedef bool (*conf_item_verifier)(void *value, char **errmsg);
typedef char *(*conf_item_formatter)(void *value);

struct conf_item {
	const char *name;
	size_t number;
	conf_item_parser parser;
	size_t offset;
	conf_item_verifier verifier;
	conf_item_formatter formatter;
};

struct env_to_conf_item {
	const char *env_name;
	const char *conf_name;
};

static bool
parse_bool(const char *str, void *result, char **errmsg)
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

static const char *
bool_to_string(bool value)
{
	return value ? "true" : "false";
}

static char *
format_bool(void *value)
{
	bool *b = (bool *)value;
	return x_strdup(bool_to_string(*b));
}

static bool
parse_env_string(const char *str, void *result, char **errmsg)
{
	char **value = (char **)result;
	free(*value);
	*value = subst_env_in_string(str, errmsg);
	return *value != NULL;
}

static char *
format_string(void *value)
{
	char **str = (char **)value;
	return x_strdup(*str);
}

static char *
format_env_string(void *value)
{
	return format_string(value);
}

static bool
parse_float(const char *str, void *result, char **errmsg)
{
	float *value = (float *)result;
	errno = 0;
	char *endptr;
	float x = strtof(str, &endptr);
	if (errno == 0 && *str != '\0' && *endptr == '\0') {
		*value = x;
		return true;
	} else {
		*errmsg = format("invalid floating point: \"%s\"", str);
		return false;
	}
}

static char *
format_float(void *value)
{
	float *x = (float *)value;
	return format("%.1f", *x);
}

static bool
parse_size(const char *str, void *result, char **errmsg)
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

static char *
format_size(void *value)
{
	uint64_t *size = (uint64_t *)value;
	return format_parsable_size_with_suffix(*size);
}

static bool
parse_sloppiness(const char *str, void *result, char **errmsg)
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
		} else if (str_eq(word, "no_system_headers")) {
			*value |= SLOPPY_NO_SYSTEM_HEADERS;
		} else if (str_eq(word, "pch_defines")) {
			*value |= SLOPPY_PCH_DEFINES;
		} else if (str_eq(word, "time_macros")) {
			*value |= SLOPPY_TIME_MACROS;
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

static char *
format_sloppiness(void *value)
{
	unsigned *sloppiness = (unsigned *)value;
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
	if (*sloppiness & SLOPPY_NO_SYSTEM_HEADERS) {
		reformat(&s, "%sno_system_headers, ", s);
	}
	if (*sloppiness) {
		// Strip last ", ".
		s[strlen(s) - 2] = '\0';
	}
	return s;
}

static bool
parse_string(const char *str, void *result, char **errmsg)
{
	(void)errmsg;

	char **value = (char **)result;
	free(*value);
	*value = x_strdup(str);
	return true;
}

static bool
parse_umask(const char *str, void *result, char **errmsg)
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

static char *
format_umask(void *value)
{
	unsigned *umask = (unsigned *)value;
	if (*umask == UINT_MAX) {
		return x_strdup("");
	} else {
		return format("%03o", *umask);
	}
}

static bool
parse_unsigned(const char *str, void *result, char **errmsg)
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

static char *
format_unsigned(void *value)
{
	unsigned *i = (unsigned *)value;
	return format("%u", *i);
}

static bool
verify_absolute_path(void *value, char **errmsg)
{
	char **path = (char **)value;
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

static bool
verify_dir_levels(void *value, char **errmsg)
{
	unsigned *levels = (unsigned *)value;
	assert(levels);
	if (*levels >= 1 && *levels <= 8) {
		return true;
	} else {
		*errmsg = format("cache directory levels must be between 1 and 8");
		return false;
	}
}

#define ITEM(name, type) \
	parse_ ## type, offsetof(struct conf, name), NULL, format_ ## type
#define ITEM_V(name, type, verification) \
	parse_ ## type, offsetof(struct conf, name), \
	verify_ ## verification, format_ ## type

#include "confitems_lookup.c"
#include "envtoconfitems_lookup.c"

static const struct conf_item *
find_conf(const char *name)
{
	return confitems_get(name, strlen(name));
}

static const struct env_to_conf_item *
find_env_to_conf(const char *name)
{
	return envtoconfitems_get(name, strlen(name));
}

static bool
handle_conf_setting(struct conf *conf, const char *key, const char *value,
                    char **errmsg, bool from_env_variable, bool negate_boolean,
                    const char *origin)
{
	const struct conf_item *item = find_conf(key);
	if (!item) {
		*errmsg = format("unknown configuration option \"%s\"", key);
		return false;
	}

	if (from_env_variable && item->parser == parse_bool) {
		// Special rule for boolean settings from the environment: "0", "false",
		// "disable" and "no" (case insensitive) are invalid, and all other values
		// mean true.
		//
		// Previously any value meant true, but this was surprising to users, who
		// might do something like CCACHE_DISABLE=0 and expect ccache to be
		// enabled.
		if (str_eq(value, "0") || strcasecmp(value, "false") == 0
		    || strcasecmp(value, "disable") == 0 || strcasecmp(value, "no") == 0) {
			fatal("invalid boolean environment variable value \"%s\"", value);
		}

		bool *boolvalue = (bool *)((char *)conf + item->offset);
		*boolvalue = !negate_boolean;
		goto out;
	}

	if (!item->parser(value, (char *)conf + item->offset, errmsg)) {
		return false;
	}
	if (item->verifier && !item->verifier((char *)conf + item->offset, errmsg)) {
		return false;
	}

out:
	conf->item_origins[item->number] = origin;
	return true;
}

static bool
parse_line(const char *line, char **key, char **value, char **errmsg)
{
#define SKIP_WS(x) while (isspace(*x)) { ++x; }

	*key = NULL;
	*value = NULL;

	const char *p = line;
	SKIP_WS(p);
	if (*p == '\0' || *p == '#') {
		return true;
	}
	const char *q = p;
	while (isalpha(*q) || *q == '_') {
		++q;
	}
	*key = x_strndup(p, q - p);
	p = q;
	SKIP_WS(p);
	if (*p != '=') {
		*errmsg = x_strdup("missing equal sign");
		free(*key);
		*key = NULL;
		return false;
	}
	++p;

	// Skip leading whitespace.
	SKIP_WS(p);
	q = p;
	while (*q) {
		++q;
	}
	// Skip trailing whitespace.
	while (isspace(q[-1])) {
		--q;
	}
	*value = x_strndup(p, q - p);

	return true;

#undef SKIP_WS
}

// Create a conf struct with default values.
struct conf *
conf_create(void)
{
	struct conf *conf = x_malloc(sizeof(*conf));
	conf->base_dir = x_strdup("");
	conf->cache_dir = format("%s/.ccache", get_home_directory());
	conf->cache_dir_levels = 2;
	conf->compiler = x_strdup("");
	conf->compiler_check = x_strdup("mtime");
	conf->compression = false;
	conf->compression_level = 6;
	conf->cpp_extension = x_strdup("");
	conf->debug = false;
	conf->direct_mode = true;
	conf->disable = false;
	conf->extra_files_to_hash = x_strdup("");
	conf->hard_link = false;
	conf->hash_dir = true;
	conf->ignore_headers_in_manifest = x_strdup("");
	conf->keep_comments_cpp = false;
	conf->limit_multiple = 0.8f;
	conf->log_file = x_strdup("");
	conf->max_files = 0;
	conf->max_size = (uint64_t)5 * 1000 * 1000 * 1000;
	conf->path = x_strdup("");
	conf->pch_external_checksum = false;
	conf->prefix_command = x_strdup("");
	conf->prefix_command_cpp = x_strdup("");
	conf->read_only = false;
	conf->read_only_direct = false;
	conf->recache = false;
	conf->run_second_cpp = true;
	conf->sloppiness = 0;
	conf->stats = true;
	conf->temporary_dir = x_strdup("");
	conf->umask = UINT_MAX; // Default: don't set umask.
	conf->unify = false;
	conf->item_origins = x_malloc(CONFITEMS_TOTAL_KEYWORDS * sizeof(char *));
	for (size_t i = 0; i < CONFITEMS_TOTAL_KEYWORDS; ++i) {
		conf->item_origins[i] = "default";
	}
	return conf;
}

void
conf_free(struct conf *conf)
{
	if (!conf) {
		return;
	}
	free(conf->base_dir);
	free(conf->cache_dir);
	free(conf->compiler);
	free(conf->compiler_check);
	free(conf->cpp_extension);
	free(conf->extra_files_to_hash);
	free(conf->ignore_headers_in_manifest);
	free(conf->log_file);
	free(conf->path);
	free(conf->prefix_command);
	free(conf->prefix_command_cpp);
	free(conf->temporary_dir);
	free((void *)conf->item_origins); // Workaround for MSVC warning
	free(conf);
}

// Note: The path pointer is stored in conf, so path must outlive conf.
//
// On failure, if an I/O error occured errno is set approriately, otherwise
// errno is set to zero indicating that config itself was invalid.
bool
conf_read(struct conf *conf, const char *path, char **errmsg)
{
	assert(errmsg);
	*errmsg = NULL;

	FILE *f = fopen(path, "r");
	if (!f) {
		*errmsg = format("%s: %s", path, strerror(errno));
		return false;
	}

	unsigned line_number = 0;
	bool result = true;
	char buf[10000];
	while (fgets(buf, sizeof(buf), f)) {
		++line_number;

		char *key;
		char *value;
		char *errmsg2;
		bool ok = parse_line(buf, &key, &value, &errmsg2);
		if (ok && key) { // key == NULL if comment or blank line.
			ok = handle_conf_setting(conf, key, value, &errmsg2, false, false, path);
		}
		free(key);
		free(value);
		if (!ok) {
			*errmsg = format("%s:%u: %s", path, line_number, errmsg2);
			free(errmsg2);
			errno = 0;
			result = false;
			goto out;
		}
	}
	if (ferror(f)) {
		*errmsg = x_strdup(strerror(errno));
		result = false;
	}

out:
	fclose(f);
	return result;
}

bool
conf_update_from_environment(struct conf *conf, char **errmsg)
{
	for (char **p = environ; *p; ++p) {
		if (!str_startswith(*p, "CCACHE_")) {
			continue;
		}
		char *q = strchr(*p, '=');
		if (!q) {
			continue;
		}

		bool negate;
		size_t key_start;
		if (str_startswith(*p + 7, "NO")) {
			negate = true;
			key_start = 9;
		} else {
			negate = false;
			key_start = 7;
		}
		char *key = x_strndup(*p + key_start, q - *p - key_start);

		++q; // Now points to the value.

		const struct env_to_conf_item *env_to_conf_item = find_env_to_conf(key);
		if (!env_to_conf_item) {
			free(key);
			continue;
		}

		char *errmsg2;
		bool ok = handle_conf_setting(
			conf, env_to_conf_item->conf_name, q, &errmsg2, true, negate,
			"environment");
		if (!ok) {
			*errmsg = format("%s: %s", key, errmsg2);
			free(errmsg2);
			free(key);
			return false;
		}

		free(key);
	}

	return true;
}

bool
conf_set_value_in_file(const char *path, const char *key, const char *value,
                       char **errmsg)
{
	const struct conf_item *item = find_conf(key);
	if (!item) {
		*errmsg = format("unknown configuration option \"%s\"", key);
		return false;
	}

	FILE *infile = fopen(path, "r");
	if (!infile) {
		*errmsg = format("%s: %s", path, strerror(errno));
		return false;
	}

	char *outpath = format("%s.tmp", path);
	FILE *outfile = create_tmp_file(&outpath, "w");
	if (!outfile) {
		*errmsg = format("%s: %s", outpath, strerror(errno));
		free(outpath);
		fclose(infile);
		return false;
	}

	bool found = false;
	char buf[10000];
	while (fgets(buf, sizeof(buf), infile)) {
		char *key2;
		char *value2;
		char *errmsg2;
		bool ok = parse_line(buf, &key2, &value2, &errmsg2);
		if (ok && key2 && str_eq(key2, key)) {
			found = true;
			fprintf(outfile, "%s = %s\n", key, value);
		} else {
			fputs(buf, outfile);
		}
		free(key2);
		free(value2);
	}

	if (!found) {
		fprintf(outfile, "%s = %s\n", key, value);
	}

	fclose(infile);
	fclose(outfile);
	if (x_rename(outpath, path) != 0) {
		*errmsg = format("rename %s to %s: %s", outpath, path, strerror(errno));
		return false;
	}
	free(outpath);

	return true;
}

bool
conf_print_value(struct conf *conf, const char *key,
                 FILE *file, char **errmsg)
{
	const struct conf_item *item = find_conf(key);
	if (!item) {
		*errmsg = format("unknown configuration option \"%s\"", key);
		return false;
	}
	void *value = (char *)conf + item->offset;
	char *str = item->formatter(value);
	fprintf(file, "%s\n", str);
	free(str);
	return true;
}

static bool
print_item(struct conf *conf, const char *key,
           void (*printer)(const char *descr, const char *origin,
                           void *context),
           void *context)
{
	const struct conf_item *item = find_conf(key);
	if (!item) {
		return false;
	}
	void *value = (char *)conf + item->offset;
	char *str = item->formatter(value);
	char *buf = x_strdup("");
	reformat(&buf, "%s = %s", key, str);
	printer(buf, conf->item_origins[item->number], context);
	free(buf);
	free(str);
	return true;
}

bool
conf_print_items(struct conf *conf,
                 void (*printer)(const char *descr, const char *origin,
                                 void *context),
                 void *context)
{
	bool ok = true;
	ok &= print_item(conf, "base_dir", printer, context);
	ok &= print_item(conf, "cache_dir", printer, context);
	ok &= print_item(conf, "cache_dir_levels", printer, context);
	ok &= print_item(conf, "compiler", printer, context);
	ok &= print_item(conf, "compiler_check", printer, context);
	ok &= print_item(conf, "compression", printer, context);
	ok &= print_item(conf, "compression_level", printer, context);
	ok &= print_item(conf, "cpp_extension", printer, context);
	ok &= print_item(conf, "debug", printer, context);
	ok &= print_item(conf, "direct_mode", printer, context);
	ok &= print_item(conf, "disable", printer, context);
	ok &= print_item(conf, "extra_files_to_hash", printer, context);
	ok &= print_item(conf, "hard_link", printer, context);
	ok &= print_item(conf, "hash_dir", printer, context);
	ok &= print_item(conf, "ignore_headers_in_manifest", printer, context);
	ok &= print_item(conf, "keep_comments_cpp", printer, context);
	ok &= print_item(conf, "limit_multiple", printer, context);
	ok &= print_item(conf, "log_file", printer, context);
	ok &= print_item(conf, "max_files", printer, context);
	ok &= print_item(conf, "max_size", printer, context);
	ok &= print_item(conf, "path", printer, context);
	ok &= print_item(conf, "pch_external_checksum", printer, context);
	ok &= print_item(conf, "prefix_command", printer, context);
	ok &= print_item(conf, "prefix_command_cpp", printer, context);
	ok &= print_item(conf, "read_only", printer, context);
	ok &= print_item(conf, "read_only_direct", printer, context);
	ok &= print_item(conf, "recache", printer, context);
	ok &= print_item(conf, "run_second_cpp", printer, context);
	ok &= print_item(conf, "sloppiness", printer, context);
	ok &= print_item(conf, "stats", printer, context);
	ok &= print_item(conf, "temporary_dir", printer, context);
	ok &= print_item(conf, "umask", printer, context);
	ok &= print_item(conf, "unify", printer, context);
	return ok;
}
