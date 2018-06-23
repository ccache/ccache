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

struct conf_item {
	const char *name;
	size_t number;
	conf_item_parser parser;
	size_t offset;
	conf_item_verifier verifier;
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

static bool
parse_env_string(const char *str, void *result, char **errmsg)
{
	char **value = (char **)result;
	free(*value);
	*value = subst_env_in_string(str, errmsg);
	return *value != NULL;
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

static const char *
bool_to_string(bool value)
{
	return value ? "true" : "false";
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
  parse_ ## type, offsetof(struct conf, name), NULL
#define ITEM_V(name, type, verification) \
  parse_ ## type, offsetof(struct conf, name), verify_ ## verification

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

		bool *value = (bool *)((char *)conf + item->offset);
		*value = !negate_boolean;
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
		if (!handle_conf_setting(
		      conf, env_to_conf_item->conf_name, q, &errmsg2, true, negate,
		      "environment")) {
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
conf_print_items(struct conf *conf,
                 void (*printer)(const char *descr, const char *origin,
                                 void *context),
                 void *context)
{
	char *s = x_strdup("");

	reformat(&s, "base_dir = %s", conf->base_dir);
	printer(s, conf->item_origins[find_conf("base_dir")->number], context);

	reformat(&s, "cache_dir = %s", conf->cache_dir);
	printer(s, conf->item_origins[find_conf("cache_dir")->number], context);

	reformat(&s, "cache_dir_levels = %u", conf->cache_dir_levels);
	printer(s, conf->item_origins[find_conf("cache_dir_levels")->number],
	        context);

	reformat(&s, "compiler = %s", conf->compiler);
	printer(s, conf->item_origins[find_conf("compiler")->number], context);

	reformat(&s, "compiler_check = %s", conf->compiler_check);
	printer(s, conf->item_origins[find_conf("compiler_check")->number], context);

	reformat(&s, "compression = %s", bool_to_string(conf->compression));
	printer(s, conf->item_origins[find_conf("compression")->number], context);

	reformat(&s, "compression_level = %u", conf->compression_level);
	printer(s, conf->item_origins[find_conf("compression_level")->number],
	        context);

	reformat(&s, "cpp_extension = %s", conf->cpp_extension);
	printer(s, conf->item_origins[find_conf("cpp_extension")->number], context);

	reformat(&s, "debug = %s", bool_to_string(conf->debug));
	printer(s, conf->item_origins[find_conf("debug")->number], context);

	reformat(&s, "direct_mode = %s", bool_to_string(conf->direct_mode));
	printer(s, conf->item_origins[find_conf("direct_mode")->number], context);

	reformat(&s, "disable = %s", bool_to_string(conf->disable));
	printer(s, conf->item_origins[find_conf("disable")->number], context);

	reformat(&s, "extra_files_to_hash = %s", conf->extra_files_to_hash);
	printer(s, conf->item_origins[find_conf("extra_files_to_hash")->number],
	        context);

	reformat(&s, "hard_link = %s", bool_to_string(conf->hard_link));
	printer(s, conf->item_origins[find_conf("hard_link")->number], context);

	reformat(&s, "hash_dir = %s", bool_to_string(conf->hash_dir));
	printer(s, conf->item_origins[find_conf("hash_dir")->number], context);

	reformat(&s, "ignore_headers_in_manifest = %s",
	         conf->ignore_headers_in_manifest);
	printer(s,
	        conf->item_origins[find_conf("ignore_headers_in_manifest")->number],
	        context);

	reformat(&s, "keep_comments_cpp = %s",
	         bool_to_string(conf->keep_comments_cpp));
	printer(s, conf->item_origins[find_conf(
	                                "keep_comments_cpp")->number], context);

	reformat(&s, "limit_multiple = %.1f", (double)conf->limit_multiple);
	printer(s, conf->item_origins[find_conf("limit_multiple")->number], context);

	reformat(&s, "log_file = %s", conf->log_file);
	printer(s, conf->item_origins[find_conf("log_file")->number], context);

	reformat(&s, "max_files = %u", conf->max_files);
	printer(s, conf->item_origins[find_conf("max_files")->number], context);

	char *s2 = format_parsable_size_with_suffix(conf->max_size);
	reformat(&s, "max_size = %s", s2);
	printer(s, conf->item_origins[find_conf("max_size")->number], context);
	free(s2);

	reformat(&s, "path = %s", conf->path);
	printer(s, conf->item_origins[find_conf("path")->number], context);

	reformat(&s, "pch_external_checksum = %s",
	         bool_to_string(conf->pch_external_checksum));
	printer(s, conf->item_origins[find_conf("pch_external_checksum")->number],
	        context);

	reformat(&s, "prefix_command = %s", conf->prefix_command);
	printer(s, conf->item_origins[find_conf("prefix_command")->number], context);

	reformat(&s, "prefix_command_cpp = %s", conf->prefix_command_cpp);
	printer(s, conf->item_origins[find_conf(
	                                "prefix_command_cpp")->number], context);

	reformat(&s, "read_only = %s", bool_to_string(conf->read_only));
	printer(s, conf->item_origins[find_conf("read_only")->number], context);

	reformat(&s, "read_only_direct = %s", bool_to_string(conf->read_only_direct));
	printer(s, conf->item_origins[find_conf("read_only_direct")->number],
	        context);

	reformat(&s, "recache = %s", bool_to_string(conf->recache));
	printer(s, conf->item_origins[find_conf("recache")->number], context);

	reformat(&s, "run_second_cpp = %s", bool_to_string(conf->run_second_cpp));
	printer(s, conf->item_origins[find_conf("run_second_cpp")->number], context);

	reformat(&s, "sloppiness = ");
	if (conf->sloppiness & SLOPPY_FILE_MACRO) {
		reformat(&s, "%sfile_macro, ", s);
	}
	if (conf->sloppiness & SLOPPY_INCLUDE_FILE_MTIME) {
		reformat(&s, "%sinclude_file_mtime, ", s);
	}
	if (conf->sloppiness & SLOPPY_INCLUDE_FILE_CTIME) {
		reformat(&s, "%sinclude_file_ctime, ", s);
	}
	if (conf->sloppiness & SLOPPY_TIME_MACROS) {
		reformat(&s, "%stime_macros, ", s);
	}
	if (conf->sloppiness & SLOPPY_PCH_DEFINES) {
		reformat(&s, "%spch_defines, ", s);
	}
	if (conf->sloppiness & SLOPPY_FILE_STAT_MATCHES) {
		reformat(&s, "%sfile_stat_matches, ", s);
	}
	if (conf->sloppiness & SLOPPY_NO_SYSTEM_HEADERS) {
		reformat(&s, "%sno_system_headers, ", s);
	}
	if (conf->sloppiness) {
		// Strip last ", ".
		s[strlen(s) - 2] = '\0';
	}
	printer(s, conf->item_origins[find_conf("sloppiness")->number], context);

	reformat(&s, "stats = %s", bool_to_string(conf->stats));
	printer(s, conf->item_origins[find_conf("stats")->number], context);

	reformat(&s, "temporary_dir = %s", conf->temporary_dir);
	printer(s, conf->item_origins[find_conf("temporary_dir")->number], context);

	if (conf->umask == UINT_MAX) {
		reformat(&s, "umask = ");
	} else {
		reformat(&s, "umask = %03o", conf->umask);
	}
	printer(s, conf->item_origins[find_conf("umask")->number], context);

	reformat(&s, "unify = %s", bool_to_string(conf->unify));
	printer(s, conf->item_origins[find_conf("unify")->number], context);

	free(s);
	return true;
}
