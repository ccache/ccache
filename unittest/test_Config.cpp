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

#include "../src/Config.hpp"
#include "../src/Error.hpp"
#include "../src/Util.hpp"
#include "../src/ccache.hpp"

#include "third_party/catch.hpp"
#include "third_party/fmt/core.h"

#include <limits>
#include <string>
#include <vector>

using Catch::Equals;

TEST_CASE("Config: default values")
{
  Config config;
  CHECK(config.base_dir().empty());
  CHECK(config.cache_dir() == std::string(get_home_directory()) + "/.ccache");
  CHECK(config.cache_dir_levels() == 2);
  CHECK(config.compiler().empty());
  CHECK(config.compiler_check() == "mtime");
  CHECK(config.compression());
  CHECK(config.compression_level() == 0);
  CHECK(config.cpp_extension().empty());
  CHECK(!config.debug());
  CHECK(!config.depend_mode());
  CHECK(config.direct_mode());
  CHECK(!config.disable());
  CHECK(config.extra_files_to_hash().empty());
  CHECK(!config.file_clone());
  CHECK(!config.hard_link());
  CHECK(config.hash_dir());
  CHECK(config.ignore_headers_in_manifest().empty());
  CHECK_FALSE(config.keep_comments_cpp());
  CHECK(config.limit_multiple() == Approx(0.8));
  CHECK(config.log_file().empty());
  CHECK(config.max_files() == 0);
  CHECK(config.max_size() == static_cast<uint64_t>(5) * 1000 * 1000 * 1000);
  CHECK(config.path().empty());
  CHECK_FALSE(config.pch_external_checksum());
  CHECK(config.prefix_command().empty());
  CHECK(config.prefix_command_cpp().empty());
  CHECK_FALSE(config.read_only());
  CHECK_FALSE(config.read_only_direct());
  CHECK_FALSE(config.recache());
  CHECK(config.run_second_cpp());
  CHECK(config.sloppiness() == 0);
  CHECK(config.stats());
  CHECK(config.temporary_dir().empty());
  CHECK(config.umask() == std::numeric_limits<uint32_t>::max());
  CHECK_FALSE(config.unify());
}

TEST_CASE("Config::update_from_file")
{
  const char user[] = "rabbit";
  x_setenv("USER", user);

#ifndef _WIN32
  std::string base_dir = fmt::format("/{0}/foo/{0}", user);
#else
  std::string base_dir = fmt::format("C:/{0}/foo/{0}", user);
#endif

  Util::write_file(
    "ccache.conf",
    "base_dir = " + base_dir + "\n"
    "cache_dir=\n"
    "cache_dir = $USER$/${USER}/.ccache\n"
    "\n"
    "\n"
    "  #A comment\n"
    " cache_dir_levels = 4\n"
    "\t compiler = foo\n"
    "compiler_check = none\n"
    "compression=false\n"
    "compression_level= 2\n"
    "cpp_extension = .foo\n"
    "depend_mode = true\n"
    "direct_mode = false\n"
    "disable = true\n"
    "extra_files_to_hash = a:b c:$USER\n"
    "file_clone = true\n"
    "hard_link = true\n"
    "hash_dir = false\n"
    "ignore_headers_in_manifest = a:b/c\n"
    "keep_comments_cpp = true\n"
    "limit_multiple = 1.0\n"
    "log_file = $USER${USER} \n"
    "max_files = 17\n"
    "max_size = 123M\n"
    "path = $USER.x\n"
    "pch_external_checksum = true\n"
    "prefix_command = x$USER\n"
    "prefix_command_cpp = y\n"
    "read_only = true\n"
    "read_only_direct = true\n"
    "recache = true\n"
    "run_second_cpp = false\n"
    "sloppiness =     time_macros   ,include_file_mtime"
    "  include_file_ctime,file_stat_matches,file_stat_matches_ctime,pch_defines"
    " ,  no_system_headers,system_headers,clang_index_store\n"
    "stats = false\n"
    "temporary_dir = ${USER}_foo\n"
    "umask = 777\n"
    "unify = true"); // Note: no newline.

  Config config;
  REQUIRE(config.update_from_file("ccache.conf"));
  CHECK(config.base_dir() == base_dir);
  CHECK(config.cache_dir() == fmt::format("{0}$/{0}/.ccache", user));
  CHECK(config.cache_dir_levels() == 4);
  CHECK(config.compiler() == "foo");
  CHECK(config.compiler_check() == "none");
  CHECK_FALSE(config.compression());
  CHECK(config.compression_level() == 2);
  CHECK(config.cpp_extension() == ".foo");
  CHECK(config.depend_mode());
  CHECK_FALSE(config.direct_mode());
  CHECK(config.disable());
  CHECK(config.extra_files_to_hash() == fmt::format("a:b c:{}", user));
  CHECK(config.file_clone());
  CHECK(config.hard_link());
  CHECK_FALSE(config.hash_dir());
  CHECK(config.ignore_headers_in_manifest() == "a:b/c");
  CHECK(config.keep_comments_cpp());
  CHECK(config.limit_multiple() == Approx(1.0));
  CHECK(config.log_file() == fmt::format("{0}{0}", user));
  CHECK(config.max_files() == 17);
  CHECK(config.max_size() == 123 * 1000 * 1000);
  CHECK(config.path() == fmt::format("{}.x", user));
  CHECK(config.pch_external_checksum());
  CHECK(config.prefix_command() == fmt::format("x{}", user));
  CHECK(config.prefix_command_cpp() == "y");
  CHECK(config.read_only());
  CHECK(config.read_only_direct());
  CHECK(config.recache());
  CHECK_FALSE(config.run_second_cpp());
  CHECK(config.sloppiness()
        == (SLOPPY_INCLUDE_FILE_MTIME | SLOPPY_INCLUDE_FILE_CTIME
            | SLOPPY_TIME_MACROS | SLOPPY_FILE_STAT_MATCHES
            | SLOPPY_FILE_STAT_MATCHES_CTIME | SLOPPY_SYSTEM_HEADERS
            | SLOPPY_PCH_DEFINES | SLOPPY_CLANG_INDEX_STORE));
  CHECK_FALSE(config.stats());
  CHECK(config.temporary_dir() == fmt::format("{}_foo", user));
  CHECK(config.umask() == 0777);
  CHECK(config.unify());
}

TEST_CASE("Config::update_from_file, error handling")
{
  Config config;
  unlink("ccache.conf"); // Make sure it doesn't exist.

  SECTION("missing equal sign")
  {
    Util::write_file("ccache.conf", "no equal sign");
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        Equals("ccache.conf:1: missing equal sign"));
  }

  SECTION("unknown key")
  {
    Util::write_file("ccache.conf", "# Comment\nfoo = bar");
    CHECK(config.update_from_file("ccache.conf"));
  }

  SECTION("invalid bool")
  {
    Util::write_file("ccache.conf", "disable=");
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        Equals("ccache.conf:1: not a boolean value: \"\""));

    Util::write_file("ccache.conf", "disable=foo");
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        Equals("ccache.conf:1: not a boolean value: \"foo\""));
  }

  SECTION("invalid variable reference")
  {
    Util::write_file("ccache.conf", "base_dir = ${foo");
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      Equals("ccache.conf:1: syntax error: missing '}' after \"foo\""));
    // Other cases tested in test_Util.c.
  }

  SECTION("empty umask")
  {
    Util::write_file("ccache.conf", "umask = ");
    CHECK(config.update_from_file("ccache.conf"));
    CHECK(config.umask() == std::numeric_limits<uint32_t>::max());
  }

  SECTION("invalid size")
  {
    Util::write_file("ccache.conf", "max_size = foo");
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        Equals("ccache.conf:1: invalid size: \"foo\""));
    // Other cases tested in test_Util.c.
  }

  SECTION("unknown sloppiness")
  {
    Util::write_file("ccache.conf", "sloppiness = time_macros, foo");
    CHECK(config.update_from_file("ccache.conf"));
    CHECK(config.sloppiness() == SLOPPY_TIME_MACROS);
  }

  SECTION("invalid unsigned")
  {
    Util::write_file("ccache.conf", "max_files =");
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      Equals("ccache.conf:1: invalid unsigned integer: \"\""));

    Util::write_file("ccache.conf", "max_files = -42");
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      Equals("ccache.conf:1: invalid unsigned integer: \"-42\""));

    Util::write_file("ccache.conf", "max_files = foo");
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      Equals("ccache.conf:1: invalid unsigned integer: \"foo\""));
  }

  SECTION("missing file")
  {
    CHECK(!config.update_from_file("ccache.conf"));
  }

  SECTION("relative base dir")
  {
    Util::write_file("ccache.conf", "base_dir = relative/path");
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      Equals("ccache.conf:1: not an absolute path: \"relative/path\""));

    Util::write_file("ccache.conf", "base_dir =");
    CHECK(config.update_from_file("ccache.conf"));
  }

  SECTION("bad dir levels")
  {
    Util::write_file("ccache.conf", "cache_dir_levels = 0");
    try {
      config.update_from_file("ccache.conf");
      CHECK(false);
    } catch (const Error& e) {
      CHECK(std::string(e.what())
            == "ccache.conf:1: cache directory levels must be between 1 and 8");
    }

    Util::write_file("ccache.conf", "cache_dir_levels = 9");
    try {
      config.update_from_file("ccache.conf");
      CHECK(false);
    } catch (const Error& e) {
      CHECK(std::string(e.what())
            == "ccache.conf:1: cache directory levels must be between 1 and 8");
    }
  }
}

TEST_CASE("Config::update_from_environment")
{
  Config config;

  x_setenv("CCACHE_COMPRESS", "1");
  config.update_from_environment();
  CHECK(config.compression());

  x_unsetenv("CCACHE_COMPRESS");

  x_setenv("CCACHE_NOCOMPRESS", "1");
  config.update_from_environment();
  CHECK(!config.compression());
}

TEST_CASE("Config::set_value_in_file")
{
  SECTION("set new value")
  {
    Util::write_file("ccache.conf", "path = vanilla\n");
    Config::set_value_in_file("ccache.conf", "compiler", "chocolate");
    std::string content = Util::read_file("ccache.conf");
    CHECK(content == "path = vanilla\ncompiler = chocolate\n");
  }

  SECTION("existing value")
  {
    Util::write_file("ccache.conf", "path = chocolate\nstats = chocolate\n");
    Config::set_value_in_file("ccache.conf", "path", "vanilla");
    std::string content = Util::read_file("ccache.conf");
    CHECK(content == "path = vanilla\nstats = chocolate\n");
  }

  SECTION("unknown option")
  {
    Util::write_file("ccache.conf", "path = chocolate\nstats = chocolate\n");
    try {
      Config::set_value_in_file("ccache.conf", "foo", "bar");
      CHECK(false);
    } catch (const Error& e) {
      CHECK(std::string(e.what()) == "unknown configuration option \"foo\"");
    }

    std::string content = Util::read_file("ccache.conf");
    CHECK(content == "path = chocolate\nstats = chocolate\n");
  }

  SECTION("unknown sloppiness")
  {
    Util::write_file("ccache.conf", "path = vanilla\n");
    Config::set_value_in_file("ccache.conf", "sloppiness", "foo");
    std::string content = Util::read_file("ccache.conf");
    CHECK(content == "path = vanilla\nsloppiness = foo\n");
  }
}

TEST_CASE("Config::get_string_value")
{
  Config config;

  SECTION("base case")
  {
    config.set_max_files(42);
    CHECK(config.get_string_value("max_files") == "42");
  }

  SECTION("unknown key")
  {
    try {
      config.get_string_value("foo");
      CHECK(false);
    } catch (const Error& e) {
      CHECK(std::string(e.what()) == "unknown configuration option \"foo\"");
    }
  }
}

TEST_CASE("Config::visit_items")
{
  Util::write_file(
    "test.conf",
#ifndef _WIN32
    "base_dir = /bd\n"
#else
    "base_dir = C:/bd\n"
#endif
    "cache_dir = cd\n"
    "cache_dir_levels = 7\n"
    "compiler = c\n"
    "compiler_check = cc\n"
    "compression = true\n"
    "compression_level = 8\n"
    "cpp_extension = ce\n"
    "debug = false\n"
    "depend_mode = true\n"
    "direct_mode = false\n"
    "disable = true\n"
    "extra_files_to_hash = efth\n"
    "file_clone = true\n"
    "hard_link = true\n"
    "hash_dir = false\n"
    "ignore_headers_in_manifest = ihim\n"
    "keep_comments_cpp = true\n"
    "limit_multiple = 0.0\n"
    "log_file = lf\n"
    "max_files = 4711\n"
    "max_size = 98.7M\n"
    "path = p\n"
    "pch_external_checksum = true\n"
    "prefix_command = pc\n"
    "prefix_command_cpp = pcc\n"
    "read_only = true\n"
    "read_only_direct = true\n"
    "recache = true\n"
    "run_second_cpp = false\n"
    "sloppiness = include_file_mtime, include_file_ctime, time_macros,"
    " file_stat_matches, file_stat_matches_ctime, pch_defines, system_headers,"
    " clang_index_store\n"
    "stats = false\n"
    "temporary_dir = td\n"
    "umask = 022\n"
    "unify = true\n");

  Config config;
  config.update_from_file("test.conf");

  std::vector<std::string> received_items;

  config.visit_items([&](const std::string& key,
                         const std::string& value,
                         const std::string& origin) {
    received_items.push_back(fmt::format("({}) {} = {}", origin, key, value));
  });

  std::vector<std::string> expected = {
#ifndef _WIN32
    "(test.conf) base_dir = /bd",
#else
    "(test.conf) base_dir = C:/bd",
#endif
    "(test.conf) cache_dir = cd",
    "(test.conf) cache_dir_levels = 7",
    "(test.conf) compiler = c",
    "(test.conf) compiler_check = cc",
    "(test.conf) compression = true",
    "(test.conf) compression_level = 8",
    "(test.conf) cpp_extension = ce",
    "(test.conf) debug = false",
    "(test.conf) depend_mode = true",
    "(test.conf) direct_mode = false",
    "(test.conf) disable = true",
    "(test.conf) extra_files_to_hash = efth",
    "(test.conf) file_clone = true",
    "(test.conf) hard_link = true",
    "(test.conf) hash_dir = false",
    "(test.conf) ignore_headers_in_manifest = ihim",
    "(test.conf) keep_comments_cpp = true",
    "(test.conf) limit_multiple = 0.0",
    "(test.conf) log_file = lf",
    "(test.conf) max_files = 4711",
    "(test.conf) max_size = 98.7M",
    "(test.conf) path = p",
    "(test.conf) pch_external_checksum = true",
    "(test.conf) prefix_command = pc",
    "(test.conf) prefix_command_cpp = pcc",
    "(test.conf) read_only = true",
    "(test.conf) read_only_direct = true",
    "(test.conf) recache = true",
    "(test.conf) run_second_cpp = false",
    "(test.conf) sloppiness = include_file_mtime, include_file_ctime,"
    " time_macros, pch_defines, file_stat_matches, file_stat_matches_ctime,"
    " system_headers, clang_index_store",
    "(test.conf) stats = false",
    "(test.conf) temporary_dir = td",
    "(test.conf) umask = 022",
    "(test.conf) unify = true",
  };

  REQUIRE(received_items.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(received_items[i] == expected[i]);
  }
}

// TODO Test that values in k_env_variable_table map to keys in
//   k_config_item_table.;
