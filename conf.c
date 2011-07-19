/*
 * Copyright (C) 2011 Joel Rosdahl
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

#include "conf.h"
#include "ccache.h"

typedef bool (*conf_item_parser)(const char *str, void *result, char **errmsg);
typedef bool (*conf_item_verifier)(void *value, char **errmsg);

struct conf_item {
	const char *name;
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
	return *value;
}

static bool
parse_octal(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	char *endptr;
	errno = 0;
	*value = strtoul(str, &endptr, 8);
	if (errno == 0 && *str != '\0' && *endptr == '\0') {
		return true;
	} else {
		*errmsg = format("not an octal integer: \"%s\"", str);
		return false;
	}
}

static bool
parse_size(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	size_t size;
	*errmsg = NULL;
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
	char *word, *p, *q, *saveptr = NULL;

	if (!str) {
		return *value;
	}
	p = x_strdup(str);
	q = p;
	while ((word = strtok_r(q, ", ", &saveptr))) {
		if (str_eq(word, "file_macro")) {
			*value |= SLOPPY_FILE_MACRO;
		} else if (str_eq(word, "include_file_mtime")) {
			*value |= SLOPPY_INCLUDE_FILE_MTIME;
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
	char **value = (char **)result;
	(void)errmsg;
	free(*value);
	*value = x_strdup(str);
	return true;
}

static bool
parse_unsigned(const char *str, void *result, char **errmsg)
{
	unsigned *value = (unsigned *)result;
	long x;
	char *endptr;
	errno = 0;
	x = strtol(str, &endptr, 10);
	if (errno == 0 && x >= 0 && x <= (unsigned)-1 && *str != '\0'
	    && *endptr == '\0') {
		*value = x;
		return true;
	} else {
		*errmsg = format("invalid unsigned integer: \"%s\"", str);
		return false;
	}
}

static bool
verify_absolute_path(void *value, char **errmsg)
{
	char **path = (char **)value;
	assert(*path);
	if (str_eq(*path, "")) {
		/* The empty string means "disable" in this case. */
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
	{#name, parse_##type, offsetof(struct conf, name), NULL}
#define ITEM_V(name, type, verification) \
	{#name, parse_##type, offsetof(struct conf, name), verify_##verification}

static const struct conf_item conf_items[] = {
	ITEM_V(base_dir, env_string, absolute_path),
	ITEM(cache_dir, env_string),
	ITEM_V(cache_dir_levels, unsigned, dir_levels),
	ITEM(compiler, string),
	ITEM(compiler_check, string),
	ITEM(compression, bool),
	ITEM(cpp_extension, string),
	ITEM(detect_shebang, bool),
	ITEM(direct_mode, bool),
	ITEM(disable, bool),
	ITEM(extra_files_to_hash, env_string),
	ITEM(hard_link, bool),
	ITEM(hash_dir, bool),
	ITEM(log_file, env_string),
	ITEM(max_files, unsigned),
	ITEM(max_size, size),
	ITEM(path, env_string),
	ITEM(prefix_command, env_string),
	ITEM(read_only, bool),
	ITEM(recache, bool),
	ITEM(run_second_cpp, bool),
	ITEM(sloppiness, sloppiness),
	ITEM(stats, bool),
	ITEM(temporary_dir, env_string),
	ITEM(umask, octal),
	ITEM(unify, bool)
};

#define ENV_TO_CONF(env_name, conf_name) \
	{#env_name, #conf_name}

static const struct env_to_conf_item env_to_conf_items[] = {
	ENV_TO_CONF(BASEDIR, base_dir),
	ENV_TO_CONF(CC, compiler),
	ENV_TO_CONF(COMPILERCHECK, compiler_check),
	ENV_TO_CONF(COMPRESS, compression),
	ENV_TO_CONF(CPP2, run_second_cpp),
	ENV_TO_CONF(DETECT_SHEBANG, detect_shebang),
	ENV_TO_CONF(DIR, cache_dir),
	ENV_TO_CONF(DIRECT, direct_mode),
	ENV_TO_CONF(DISABLE, disable),
	ENV_TO_CONF(EXTENSION, cpp_extension),
	ENV_TO_CONF(EXTRAFILES, extra_files_to_hash),
	ENV_TO_CONF(HARDLINK, hard_link),
	ENV_TO_CONF(HASHDIR, hash_dir),
	ENV_TO_CONF(LOGFILE, log_file),
	ENV_TO_CONF(MAXFILES, max_files),
	ENV_TO_CONF(MAXSIZE, max_size),
	ENV_TO_CONF(NLEVELS, cache_dir_levels),
	ENV_TO_CONF(PATH, path),
	ENV_TO_CONF(PREFIX, prefix_command),
	ENV_TO_CONF(READONLY, read_only),
	ENV_TO_CONF(RECACHE, recache),
	ENV_TO_CONF(SLOPPINESS, sloppiness),
	ENV_TO_CONF(STATS, stats),
	ENV_TO_CONF(TEMPDIR, temporary_dir),
	ENV_TO_CONF(UMASK, umask),
	ENV_TO_CONF(UNIFY, unify)
};

static int
compare_conf_items(const void *key1, const void *key2)
{
	const struct conf_item *conf1 = (const struct conf_item *)key1;
	const struct conf_item *conf2 = (const struct conf_item *)key2;
	return strcmp(conf1->name, conf2->name);
}

static const struct conf_item *
find_conf(const char *name)
{
	struct conf_item key;
	key.name = name;
	return bsearch(
		&key, conf_items, sizeof(conf_items) / sizeof(conf_items[0]),
		sizeof(conf_items[0]), compare_conf_items);
}

static int
compare_env_to_conf_items(const void *key1, const void *key2)
{
	const struct env_to_conf_item *conf1 = (const struct env_to_conf_item *)key1;
	const struct env_to_conf_item *conf2 = (const struct env_to_conf_item *)key2;
	return strcmp(conf1->env_name, conf2->env_name);
}

static const struct env_to_conf_item *
find_env_to_conf(const char *name)
{
	struct env_to_conf_item key;
	key.env_name = name;
	return bsearch(
		&key,
		env_to_conf_items,
		sizeof(env_to_conf_items) / sizeof(env_to_conf_items[0]),
		sizeof(env_to_conf_items[0]),
		compare_env_to_conf_items);
}

static bool
handle_conf_setting(struct conf *conf, const char *key, const char *value,
                    char **errmsg, bool from_env_variable, bool negate_boolean)
{
	const struct conf_item *item;

	item = find_conf(key);
	if (!item) {
		*errmsg = format("unknown configuration option \"%s\"", key);
		return false;
	}

	if (from_env_variable && item->parser == parse_bool) {
		/*
		 * Special rule for boolean settings from the environment: any value means
		 * true.
		 */
		bool *value = (bool *)((void *)conf + item->offset);
		*value = !negate_boolean;
		return true;
	}

	if (!item->parser(value, (void *)conf + item->offset, errmsg)) {
		return false;
	}
	if (item->verifier && !item->verifier((void *)conf + item->offset, errmsg)) {
		return false;
	}

	return true;
}

static bool
parse_line(const char *line, char **key, char **value, char **errmsg)
{
	const char *p, *q;

#define SKIP_WS(x) while (isspace(*x)) { ++x; }

	p = line;
	SKIP_WS(p);
	if (*p == '\0' || *p == '#') {
		*key = NULL;
		*value = NULL;
		return true;
	}
	q = p;
	while (isalpha(*q) || *q == '_') {
		++q;
	}
	*key = x_strndup(p, q - p);
	p = q;
	SKIP_WS(p);
	if (*p != '=') {
		*errmsg = x_strdup("missing equal sign");
		free(*key);
		return false;
	}
	++p;

	/* Skip leading whitespace. */
	SKIP_WS(p);
	q = p;
	while (*q) {
		++q;
	}
	/* Skip trailing whitespace. */
	while (isspace(q[-1])) {
		--q;
	}
	*value = x_strndup(p, q - p);

	return true;

#undef SKIP_WS
}

/* For test purposes. */
bool
conf_verify_sortedness(void)
{
	size_t i;
	for (i = 1; i < sizeof(conf_items)/sizeof(conf_items[0]); i++) {
		if (strcmp(conf_items[i-1].name, conf_items[i].name) >= 0) {
			fprintf(stderr,
			        "conf_verify_sortedness: %s >= %s\n",
			        conf_items[i-1].name,
			        conf_items[i].name);
			return false;
		}
	}
	return true;
}

/* For test purposes. */
bool
conf_verify_env_table_correctness(void)
{
	size_t i;
	for (i = 0;
	     i < sizeof(env_to_conf_items) / sizeof(env_to_conf_items[0]);
	     i++) {
		if (i > 0
		    && strcmp(env_to_conf_items[i-1].env_name,
		              env_to_conf_items[i].env_name) >= 0) {
			fprintf(stderr,
			        "conf_verify_env_table_correctness: %s >= %s\n",
			        env_to_conf_items[i-1].env_name,
			        env_to_conf_items[i].env_name);
			return false;
		}
		if (!find_conf(env_to_conf_items[i].conf_name)) {
			fprintf(stderr,
			        "conf_verify_env_table_correctness: %s -> %s,"
			        " which doesn't exist\n",
			        env_to_conf_items[i].env_name,
			        env_to_conf_items[i].conf_name);
			return false;
		}
	}
	return true;
}

/* Create a conf struct with default values. */
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
	conf->cpp_extension = x_strdup("");
	conf->detect_shebang = false;
	conf->direct_mode = true;
	conf->disable = false;
	conf->extra_files_to_hash = x_strdup("");
	conf->hard_link = false;
	conf->hash_dir = false;
	conf->log_file = x_strdup("");
	conf->max_files = 0;
	conf->max_size = 1024 * 1024; /* kilobyte */
	conf->path = x_strdup("");
	conf->prefix_command = x_strdup("");
	conf->read_only = false;
	conf->recache = false;
	conf->run_second_cpp = false;
	conf->sloppiness = 0;
	conf->stats = true;
	conf->temporary_dir = x_strdup("");
	conf->umask = UINT_MAX; /* default: don't set umask */
	conf->unify = false;
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
	free(conf->log_file);
	free(conf->path);
	free(conf->prefix_command);
	free(conf->temporary_dir);
	free(conf);
}

bool
conf_read(struct conf *conf, const char *path, char **errmsg)
{
	FILE *f;
	char buf[10000];
	bool result = true;
	unsigned line_number;

	assert(errmsg);
	*errmsg = NULL;

	f = fopen(path, "r");
	if (!f) {
		*errmsg = format("%s: %s", path, strerror(errno));
		return false;
	}

	line_number = 0;
	while (fgets(buf, sizeof(buf), f)) {
		char *errmsg2, *key, *value;
		bool ok;
		++line_number;
		ok = parse_line(buf, &key, &value, &errmsg2);
		if (ok && key) { /* key == NULL if comment or blank line */
			ok = handle_conf_setting(conf, key, value, &errmsg2, false, false);
		}
		if (!ok) {
			*errmsg = format("%s:%u: %s", path, line_number, errmsg2);
			free(errmsg2);
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
	char **p;
	char *q;
	char *key;
	char *errmsg2;
	const struct env_to_conf_item *env_to_conf_item;
	bool negate;
	size_t key_start;

	for (p = environ; *p; ++p) {
		if (!str_startswith(*p, "CCACHE_")) {
			continue;
		}
		q = strchr(*p, '=');
		if (!q) {
			continue;
		}

		if (str_startswith(*p + 7, "NO")) {
			negate = true;
			key_start = 9;
		} else {
			negate = false;
			key_start = 7;
		}
		key = x_strndup(*p + key_start, q - *p - key_start);

		++q; /* Now points to the value. */

		env_to_conf_item = find_env_to_conf(key);
		if (!env_to_conf_item) {
			free(key);
			continue;
		}

		if (!handle_conf_setting(
			    conf, env_to_conf_item->conf_name, q, &errmsg2, true, negate)) {
			*errmsg = format("%s: %s", key, errmsg2);
			free(errmsg2);
			free(key);
			return false;
		}

		free(key);
	}

	return true;
}
