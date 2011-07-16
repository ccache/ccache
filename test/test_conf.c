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

TEST_SUITE(conf)

TEST(conf_item_table_should_be_sorted)
{
	bool conf_verify_sortedness();
	CHECK(conf_verify_sortedness());
}

TEST(conf_env_item_table_should_be_sorted_and_otherwise_correct)
{
	bool conf_verify_env_table_correctness();
	CHECK(conf_verify_env_table_correctness());
}

TEST(conf_create)
{
	struct conf *conf = conf_create();
	CHECK_STR_EQ("", conf->base_dir);
	CHECK_STR_EQ_FREE1(format("%s/.ccache", get_home_directory()),
	                   conf->cache_dir);
	CHECK_UNS_EQ(2, conf->cache_dir_levels);
	CHECK_STR_EQ("", conf->compiler);
	CHECK_STR_EQ("mtime", conf->compiler_check);
	CHECK(!conf->compression);
	CHECK_STR_EQ("", conf->cpp_extension);
	CHECK(!conf->detect_shebang);
	CHECK(conf->direct_mode);
	CHECK(!conf->disable);
	CHECK_STR_EQ("", conf->extra_files_to_hash);
	CHECK(!conf->hard_link);
	CHECK(!conf->hash_dir);
	CHECK_STR_EQ("", conf->log_file);
	CHECK_UNS_EQ(0, conf->max_files);
	CHECK_UNS_EQ(1024*1024, conf->max_size);
	CHECK_STR_EQ("", conf->path);
	CHECK_STR_EQ("", conf->prefix_command);
	CHECK(!conf->read_only);
	CHECK(!conf->recache);
	CHECK(!conf->run_second_cpp);
	CHECK_UNS_EQ(0, conf->sloppiness);
	CHECK(conf->stats);
	CHECK_STR_EQ("", conf->temporary_dir);
	CHECK_UNS_EQ(UINT_MAX, conf->umask);
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
		"detect_shebang = true\n"
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
	CHECK_UNS_EQ(4, conf->cache_dir_levels);
	CHECK_STR_EQ("foo", conf->compiler);
	CHECK_STR_EQ("none", conf->compiler_check);
	CHECK(conf->compression);
	CHECK_STR_EQ(".foo", conf->cpp_extension);
	CHECK(conf->detect_shebang);
	CHECK(!conf->direct_mode);
	CHECK(conf->disable);
	CHECK_STR_EQ_FREE1(format("a:b c:%s", user), conf->extra_files_to_hash);
	CHECK(conf->hard_link);
	CHECK(conf->hash_dir);
	CHECK_STR_EQ_FREE1(format("%s%s", user, user), conf->log_file);
	CHECK_UNS_EQ(17, conf->max_files);
	CHECK_UNS_EQ(123 * 1024, conf->max_size);
	CHECK_STR_EQ_FREE1(format("%s.x", user), conf->path);
	CHECK_STR_EQ_FREE1(format("x%s", user), conf->prefix_command);
	CHECK(conf->read_only);
	CHECK(conf->recache);
	CHECK(conf->run_second_cpp);
	CHECK_UNS_EQ(SLOPPY_INCLUDE_FILE_MTIME|SLOPPY_FILE_MACRO|SLOPPY_TIME_MACROS,
	             conf->sloppiness);
	CHECK(!conf->stats);
	CHECK_STR_EQ_FREE1(format("%s_foo", user), conf->temporary_dir);
	CHECK_UNS_EQ(0777, conf->umask);
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

TEST(conf_read_invalid_octal)
{
	struct conf *conf = conf_create();
	char *errmsg;
	create_file("ccache.conf", "umask = 890x");
	CHECK(!conf_read(conf, "ccache.conf", &errmsg));
	CHECK_STR_EQ_FREE2("ccache.conf:1: not an octal integer: \"890x\"",
	                   errmsg);
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

TEST_SUITE_END
