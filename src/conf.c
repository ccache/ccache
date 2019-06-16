// Copyright (C) 2011-2019 Joel Rosdahl
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
#include "confitems.h"
#include "envtoconfitems.h"
#include "ccache.h"

enum handle_conf_result {
	HANDLE_CONF_OK,
	HANDLE_CONF_UNKNOWN,
	HANDLE_CONF_FAIL
};

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

static enum handle_conf_result
handle_conf_setting(struct conf *conf, const char *key, const char *value,
                    char **errmsg, bool from_env_variable, bool negate_boolean,
                    const char *origin)
{
	const struct conf_item *item = find_conf(key);
	if (!item) {
		return HANDLE_CONF_UNKNOWN;
	}

	if (from_env_variable && item->parser == confitem_parse_bool) {
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
		return HANDLE_CONF_FAIL;
	}
	if (item->verifier && !item->verifier((char *)conf + item->offset, errmsg)) {
		return HANDLE_CONF_FAIL;
	}

out:
	conf->item_origins[item->number] = origin;
	return HANDLE_CONF_OK;
}

static bool
parse_line(const char *line, char **key, char **value, char **errmsg)
{
#define SKIP_WS(x) do { while (isspace(*x)) { ++x; } } while (false)

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
#ifdef USE_ZSTD
	conf->compression_level = 3;
#else
	conf->compression_level = 6;
#endif
	conf->cpp_extension = x_strdup("");
	conf->debug = false;
	conf->depend_mode = false;
	conf->direct_mode = true;
	conf->disable = false;
	conf->extra_files_to_hash = x_strdup("");
	conf->hash_dir = true;
	conf->ignore_headers_in_manifest = x_strdup("");
	conf->keep_comments_cpp = false;
	conf->limit_multiple = 0.8;
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
	conf->item_origins = x_malloc(confitems_count() * sizeof(char *));
	for (size_t i = 0; i < confitems_count(); ++i) {
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
// On failure, if an I/O error occurred errno is set appropriately, otherwise
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
		enum handle_conf_result hcr = HANDLE_CONF_OK;
		bool ok = parse_line(buf, &key, &value, &errmsg2);
		if (ok && key) { // key == NULL if comment or blank line.
			hcr =
				handle_conf_setting(conf, key, value, &errmsg2, false, false, path);
			ok = hcr != HANDLE_CONF_FAIL; // unknown is OK
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

		char *errmsg2 = NULL;
		enum handle_conf_result hcr = handle_conf_setting(
			conf, env_to_conf_item->conf_name, q, &errmsg2, true, negate,
			"environment");
		if (hcr != HANDLE_CONF_OK) {
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

	char dummy[8] = {0}; // The maximum entry size in struct conf.
	if (!item->parser(value, (void *)dummy, errmsg)
	    || (item->verifier && !item->verifier(value, errmsg))) {
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
	ok &= print_item(conf, "depend_mode", printer, context);
	ok &= print_item(conf, "direct_mode", printer, context);
	ok &= print_item(conf, "disable", printer, context);
	ok &= print_item(conf, "extra_files_to_hash", printer, context);
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
