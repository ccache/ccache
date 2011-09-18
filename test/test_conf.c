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
#include "test/framework.h"
#include "test/util.h"

#define N_CONFIG_ITEMS 25
static struct {
	char *descr;
	const char *origin;
} received_conf_items[N_CONFIG_ITEMS];
static size_t n_received_conf_items = 0;

static void
conf_item_receiver(const char *descr, const char *origin, void *context)
{
	(void)context;
	received_conf_items[n_received_conf_items].descr = x_strdup(descr);
	received_conf_items[n_received_conf_items].origin = origin;
	++n_received_conf_items;
}

static void
free_received_conf_items(void)
{
	while (n_received_conf_items > 0) {
		--n_received_conf_items;
		free(received_conf_items[n_received_conf_items].descr);
	}
}

TEST_SUITE(conf)

TEST(conf_create)
{
	struct conf *conf = conf_create();
	CHECK_STR_EQ("", conf->base_dir);
	CHECK_STR_EQ_FREE1(format("%s/.ccache", get_home_directory()),
	                   conf->cache_dir);
	CHECK_INT_EQ(2, conf->cache_dir_levels);
	CHECK_STR_EQ("", conf->compiler);
	CHECK_STR_EQ("mtime", conf->compiler_check);
	CHECK(!conf->compression);
	CHECK_STR_EQ("", conf->cpp_extension);
	CHECK(conf->direct_mode);
	CHECK(!conf->disable);
	CHECK_STR_EQ("", conf->extra_files_to_hash);
	CHECK(!conf->hard_link);
	CHECK(!conf->hash_dir);
	CHECK_STR_EQ("", conf->log_file);
	CHECK_INT_EQ(0, conf->max_files);
	CHECK_INT_EQ((uint64_t)5 * 1000 * 1000 * 1000, conf->max_size);
	CHECK_STR_EQ("", conf->path);
	CHECK_STR_EQ("", conf->prefix_command);
	CHECK(!conf->read_only);
	CHECK(!conf->recache);
	CHECK(!conf->run_second_cpp);
	CHECK_INT_EQ(0, conf->sloppiness);
	CHECK(conf->stats);
	CHECK_STR_EQ("", conf->temporary_dir);
	CHECK_INT_EQ(UINT_MAX, conf->umask);
	CHECK(!conf->unify);
	conf_free(conf);
}

TEST(conf_read_valid_config)
{
	struct conf *conf = conf_create();
	char *errmsg;
	const char *user = getenv("USER");
	CHECK(user);
	create_file(
		"ccache.conf",
		"base_dir =  /$USER/foo/${USER} \n"
		"cache_dir=\n"
		"cache_dir = $USER$/${USER}/.ccache\n"
		"\n"
		"\n"
		"  #A comment\n"
		" cache_dir_levels = 4\n"
		"\t compiler = foo\n"
		"compiler_check = none\n"
		"compression=true\n"
		"cpp_extension = .foo\n"
		"direct_mode = false\n"
		"disable = true\n"
		"extra_files_to_hash = a:b c:$USER\n"
		"hard_link = true\n"
		"hash_dir = true\n"
		"log_file = $USER${USER} \n"
		"max_files = 17\n"
		"max_size = 123M\n"
		"path = $USER.x\n"
		"prefix_command = x$USER\n"
		"read_only = true\n"
		"recache = true\n"
		"run_second_cpp = true\n"
		"sloppiness =     file_macro   ,time_macros,  include_file_mtime  \n"
		"stats = false\n"
		"temporary_dir = ${USER}_foo\n"
		"umask = 777\n"
		"unify = true"); /* Note: no newline */
	CHECK(conf_read(conf, "ccache.conf", &errmsg));
	CHECK(!errmsg);

	CHECK_STR_EQ_FREE1(format("/%s/foo/%s", user, user), conf->base_dir);
	CHECK_STR_EQ_FREE1(format("%s$/%s/.ccache", user, user), conf->cache_dir);
	CHECK_INT_EQ(4, conf->cache_dir_levels);
	CHECK_STR_EQ("foo", conf->compiler);
	CHECK_STR_EQ("none", conf->compiler_check);
	CHECK(conf->compression);
	CHECK_STR_EQ(".foo", conf->cpp_extension);
	CHECK(!conf->direct_mode);
	CHECK(conf->disable);
	CHECK_STR_EQ_FREE1(format("a:b c:%s", user), conf->extra_files_to_hash);
	CHECK(conf->hard_link);
	CHECK(conf->hash_dir);
	CHECK_STR_EQ_FREE1(format("%s%s", user, user), conf->log_file);
	CHECK_INT_EQ(17, conf->max_files);
	CHECK_INT_EQ(123 * 1000 * 1000, conf->max_size);
	CHECK_STR_EQ_FREE1(format("%s.x", user), conf->path);
	CHECK_STR_EQ_FREE1(format("x%s", user), conf->prefix_command);
	CHECK(conf->read_only);
	CHECK(conf->recache);
	CHECK(conf->run_second_cpp);
	CHECK_INT_EQ(SLOPPY_INCLUDE_FILE_MTIME|SLOPPY_FILE_MACRO|SLOPPY_TIME_MACROS,
	             conf->sloppiness);
	CHECK(!conf->stats);
	CHECK_STR_EQ_FREE1(format("%s_foo", user), conf->temporary_dir);
	CHECK_INT_EQ(0777, conf->umask);
	CHECK(conf->unify);

	conf_free(conf);
}

TEST(conf_read_with_missing_equal_sign)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "no equal sign");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: missing equal sign",
	                   errmsg);
	conf_free(conf);
}

TEST(conf_read_with_bad_config_key)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "# Comment\nfoo = bar");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:2: unknown configuration option \"foo\"",
	                   errmsg);
	conf_free(conf);
}

TEST(conf_read_invalid_bool)
{
	struct conf *conf = conf_create();
	char *errmsg;

	create_file("ccache.conf", "disable=");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: not a boolean value: \"\"",
	                   errmsg);

	create_file("ccache.conf", "disable=foo");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: not a boolean value: \"foo\"",
	                   errmsg);
	conf_free(conf);
}

TEST(conf_read_invalid_env_string)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "base_dir = ${foo");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: syntax error: missing '}' after \"foo\"",
	                   errmsg);
	/* Other cases tested in test_util.c. */
	conf_free(conf);
}

TEST(conf_read_empty_umask)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "umask = ");
	CHECK(conf_read(conf, "ccache.conf", &errmsg));
	CHECK_INT_EQ(conf->umask, UINT_MAX);
	conf_free(conf);
}

TEST(conf_read_invalid_size)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "max_size = foo");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: invalid size: \"foo\"",
	                   errmsg);
	/* Other cases tested in test_util.c. */
	conf_free(conf);
}

TEST(conf_read_invalid_sloppiness)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "sloppiness = file_macro, foo");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: unknown sloppiness: \"foo\"",
	                   errmsg);
	conf_free(conf);
}

TEST(conf_read_invalid_unsigned)
{
	struct conf *conf = conf_create();
	char *errmsg;

	create_file("ccache.conf", "max_files =");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: invalid unsigned integer: \"\"",
	                   errmsg);

	create_file("ccache.conf", "max_files = -42");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: invalid unsigned integer: \"-42\"",
	                   errmsg);

	create_file("ccache.conf", "max_files = 4294967296");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: invalid unsigned integer: \"4294967296\"",
	                   errmsg);

	create_file("ccache.conf", "max_files = foo");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: invalid unsigned integer: \"foo\"",
	                   errmsg);

	conf_free(conf);
}

TEST(verify_absolute_base_dir)
{
	struct conf *conf = conf_create();
	char *errmsg;

	create_file("ccache.conf", "base_dir = relative/path");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: not an absolute path: \"relative/path\"",
	                   errmsg);

	create_file("ccache.conf", "base_dir =");
	CHECK(conf_read(conf, "ccache.conf", &errmsg));

	conf_free(conf);
}

TEST(verify_dir_levels)
{
	struct conf *conf = conf_create();
	char *errmsg;

	create_file("ccache.conf", "cache_dir_levels = 0");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: cache directory levels must be between 1 and 8",
	                   errmsg);
	create_file("ccache.conf", "cache_dir_levels = 9");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: cache directory levels must be between 1 and 8",
	                   errmsg);

	conf_free(conf);
}

TEST(conf_update_from_environment)
{
	struct conf *conf = conf_create();
	char *errmsg;

	putenv("CCACHE_COMPRESS=1");
	CHECK(conf_update_from_environment(conf, &errmsg));
	CHECK(conf->compression);

	unsetenv("CCACHE_COMPRESS");
	putenv("CCACHE_NOCOMPRESS=1");
	CHECK(conf_update_from_environment(conf, &errmsg));
	CHECK(!conf->compression);

	conf_free(conf);
}

TEST(conf_set_new_value)
{
	char *errmsg;
	char *data;

	create_file("ccache.conf", "path = vanilla\n");
	CHECK(conf_set_value_in_file("ccache.conf", "stats", "chocolate", &errmsg));
	data = read_text_file("ccache.conf", 0);
	CHECK(data);
	CHECK_STR_EQ_FREE2("path = vanilla\nstats = chocolate\n", data);
}

TEST(conf_set_existing_value)
{
	char *errmsg;
	char *data;

	create_file("ccache.conf", "path = chocolate\nstats = chocolate\n");
	CHECK(conf_set_value_in_file("ccache.conf", "path", "vanilla", &errmsg));
	data = read_text_file("ccache.conf", 0);
	CHECK(data);
	CHECK_STR_EQ_FREE2("path = vanilla\nstats = chocolate\n", data);
}

TEST(conf_print_items)
{
	size_t i;
	struct conf conf = {
		"bd",
		"cd",
		7,
		"c",
		"cc",
		true,
		"ce",
		false,
		true,
		"efth",
		true,
		true,
		"lf",
		4711,
		98.7 * 1000 * 1000,
		"p",
		"pc",
		true,
		true,
		true,
		SLOPPY_FILE_MACRO|SLOPPY_INCLUDE_FILE_MTIME|SLOPPY_TIME_MACROS,
		false,
		"td",
		022,
		true,
		NULL
	};
	size_t n = 0;

	conf.item_origins = x_malloc(N_CONFIG_ITEMS * sizeof(char *));
	for (i = 0; i < N_CONFIG_ITEMS; ++i) {
		conf.item_origins[i] = format("origin%zu", i);
	}

	conf_print_items(&conf, conf_item_receiver, NULL);
	CHECK_INT_EQ(25, n_received_conf_items);
	CHECK_STR_EQ("base_dir = bd", received_conf_items[n++].descr);
	CHECK_STR_EQ("cache_dir = cd", received_conf_items[n++].descr);
	CHECK_STR_EQ("cache_dir_levels = 7", received_conf_items[n++].descr);
	CHECK_STR_EQ("compiler = c", received_conf_items[n++].descr);
	CHECK_STR_EQ("compiler_check = cc", received_conf_items[n++].descr);
	CHECK_STR_EQ("compression = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("cpp_extension = ce", received_conf_items[n++].descr);
	CHECK_STR_EQ("direct_mode = false", received_conf_items[n++].descr);
	CHECK_STR_EQ("disable = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("extra_files_to_hash = efth", received_conf_items[n++].descr);
	CHECK_STR_EQ("hard_link = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("hash_dir = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("log_file = lf", received_conf_items[n++].descr);
	CHECK_STR_EQ("max_files = 4711", received_conf_items[n++].descr);
	CHECK_STR_EQ("max_size = 98.7M", received_conf_items[n++].descr);
	CHECK_STR_EQ("path = p", received_conf_items[n++].descr);
	CHECK_STR_EQ("prefix_command = pc", received_conf_items[n++].descr);
	CHECK_STR_EQ("read_only = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("recache = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("run_second_cpp = true", received_conf_items[n++].descr);
	CHECK_STR_EQ("sloppiness = file_macro, include_file_mtime, time_macros",
	             received_conf_items[n++].descr);
	CHECK_STR_EQ("stats = false", received_conf_items[n++].descr);
	CHECK_STR_EQ("temporary_dir = td", received_conf_items[n++].descr);
	CHECK_STR_EQ("umask = 022", received_conf_items[n++].descr);
	CHECK_STR_EQ("unify = true", received_conf_items[n++].descr);

	for (i = 0; i < N_CONFIG_ITEMS; ++i) {
		char *expected = format("origin%zu", i);
		CHECK_STR_EQ(expected, received_conf_items[i].origin);
	}

	free_received_conf_items();
	free(conf.item_origins);
}

TEST_SUITE_END
