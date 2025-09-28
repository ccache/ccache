// Copyright (C) 2011-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "testutil.hpp"

#include <ccache/config.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>

#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <vector>

namespace fs = util::filesystem;

using doctest::Approx;
using TestUtil::TestContext;

TEST_SUITE_BEGIN("Config");

TEST_CASE("Config: default values")
{
  Config config;

  CHECK(config.base_dirs().empty());
  CHECK(config.cache_dir().empty()); // Set later
  CHECK(config.compiler().empty());
  CHECK(config.compiler_check() == "mtime");
  CHECK(config.compiler_type() == CompilerType::auto_guess);
  CHECK(config.compression());
  CHECK(config.compression_level() == 0);
  CHECK(config.cpp_extension().empty());
  CHECK(!config.debug());
  CHECK(config.debug_dir().empty());
  CHECK(config.debug_level() == 2);
  CHECK(!config.depend_mode());
  CHECK(config.direct_mode());
  CHECK(!config.disable());
  CHECK(config.extra_files_to_hash().empty());
  CHECK(!config.file_clone());
  CHECK(!config.hard_link());
  CHECK(config.hash_dir());
  CHECK(config.ignore_headers_in_manifest().empty());
  CHECK(config.ignore_options().empty());
#ifndef _WIN32
  CHECK(config.inode_cache());
#else
  CHECK_FALSE(config.inode_cache());
#endif
  CHECK_FALSE(config.keep_comments_cpp());
  CHECK(config.log_file().empty());
  CHECK(config.max_files() == 0);
  CHECK(config.max_size() == static_cast<uint64_t>(5) * 1024 * 1024 * 1024);
  CHECK(config.msvc_dep_prefix() == "Note: including file:");
  CHECK(config.path().empty());
  CHECK_FALSE(config.pch_external_checksum());
  CHECK(config.prefix_command().empty());
  CHECK(config.prefix_command_cpp().empty());
  CHECK_FALSE(config.read_only());
  CHECK_FALSE(config.read_only_direct());
  CHECK_FALSE(config.recache());
  CHECK_FALSE(config.remote_only());
  CHECK(config.remote_storage().empty());
  CHECK_FALSE(config.reshare());
  CHECK(config.sloppiness().to_bitmask() == 0);
  CHECK(config.stats());
  CHECK(config.temporary_dir().empty()); // Set later
  CHECK(config.umask() == std::nullopt);
}

TEST_CASE("Config::update_from_file")
{
  TestContext test_context;

  const char user[] = "rabbit";
  util::setenv("USER", user);

#ifndef _WIN32
  std::string base_dir = FMT("/{0}/foo/{0}", user);
#else
  std::string base_dir = FMT("C:/{0}/foo/{0}", user);
#endif

  REQUIRE(util::write_file(
    "ccache.conf",
    "base_dir = " + base_dir
      + "\n"
        "cache_dir=\n"
        "cache_dir = $USER$/${USER}/.ccache\n"
        "\n"
        "\n"
        "  #A comment\n"
        "\t compiler = foo\n"
        "compiler_check = none\n"
        "compiler_type = nvcc\n"
        "compression=false\n"
        "compression_level= 2\n"
        "cpp_extension = .foo\n"
        "debug_dir = $USER$/${USER}/.ccache_debug\n"
        "debug_level = 2\n"
        "depend_mode = true\n"
        "direct_mode = false\n"
        "disable = true\n"
        "extra_files_to_hash = a:b c:$USER\n"
        "file_clone = true\n"
        "hard_link = true\n"
        "hash_dir = false\n"
        "ignore_headers_in_manifest = a:b/c\n"
        "ignore_options = -a=* -b\n"
        "inode_cache = false\n"
        "keep_comments_cpp = true\n"
        "log_file = $USER${USER} \n"
        "max_files = 17\n"
        "max_size = 123M\n"
        "msvc_dep_prefix = Some other prefix:\n"
        "path = $USER.x\n"
        "pch_external_checksum = true\n"
        "prefix_command = x$USER\n"
        "prefix_command_cpp = y\n"
        "read_only = true\n"
        "read_only_direct = true\n"
        "recache = true\n"
        "reshare = true\n"
        "sloppiness =     time_macros   ,include_file_mtime"
        "  "
        "include_file_ctime,file_stat_matches,file_stat_matches_ctime,pch_"
        "defines"
        " ,  no_system_headers,system_headers,clang_index_store,ivfsoverlay,"
        " gcno_cwd,\n"
        "stats = false\n"
        "temporary_dir = ${USER}_foo\n"
        "umask = 777")); // Note: no newline.

  Config config;
  REQUIRE(config.update_from_file("ccache.conf"));
  CHECK(config.base_dirs() == std::vector<fs::path>{base_dir});
  CHECK(config.cache_dir() == FMT("{0}$/{0}/.ccache", user));
  CHECK(config.compiler() == "foo");
  CHECK(config.compiler_check() == "none");
  CHECK(config.compiler_type() == CompilerType::nvcc);
  CHECK_FALSE(config.compression());
  CHECK(config.compression_level() == 2);
  CHECK(config.cpp_extension() == ".foo");
  CHECK(config.debug_dir() == FMT("{0}$/{0}/.ccache_debug", user));
  CHECK(config.debug_level() == 2);
  CHECK(config.depend_mode());
  CHECK_FALSE(config.direct_mode());
  CHECK(config.disable());
  CHECK(config.extra_files_to_hash() == FMT("a:b c:{}", user));
  CHECK(config.file_clone());
  CHECK(config.hard_link());
  CHECK_FALSE(config.hash_dir());
  CHECK(config.ignore_headers_in_manifest() == "a:b/c");
  CHECK(config.ignore_options() == "-a=* -b");
  CHECK_FALSE(config.inode_cache());
  CHECK(config.keep_comments_cpp());
  CHECK(config.log_file() == FMT("{0}{0}", user));
  CHECK(config.max_files() == 17);
  CHECK(config.max_size() == 123 * 1000 * 1000);
  CHECK(config.msvc_dep_prefix() == "Some other prefix:");
  CHECK(config.path() == FMT("{}.x", user));
  CHECK(config.pch_external_checksum());
  CHECK(config.prefix_command() == FMT("x{}", user));
  CHECK(config.prefix_command_cpp() == "y");
  CHECK(config.read_only());
  CHECK(config.read_only_direct());
  CHECK(config.recache());
  CHECK(config.reshare());
  CHECK(config.sloppiness().to_bitmask()
        == (static_cast<uint32_t>(core::Sloppy::clang_index_store)
            | static_cast<uint32_t>(core::Sloppy::file_stat_matches)
            | static_cast<uint32_t>(core::Sloppy::file_stat_matches_ctime)
            | static_cast<uint32_t>(core::Sloppy::gcno_cwd)
            | static_cast<uint32_t>(core::Sloppy::include_file_ctime)
            | static_cast<uint32_t>(core::Sloppy::include_file_mtime)
            | static_cast<uint32_t>(core::Sloppy::ivfsoverlay)
            | static_cast<uint32_t>(core::Sloppy::pch_defines)
            | static_cast<uint32_t>(core::Sloppy::system_headers)
            | static_cast<uint32_t>(core::Sloppy::time_macros)));
  CHECK_FALSE(config.stats());
  CHECK(config.temporary_dir() == FMT("{}_foo", user));
  CHECK(config.umask() == 0777U);
}

TEST_CASE("Config::update_from_file, error handling")
{
  TestContext test_context;

  Config config;

  SUBCASE("missing equal sign")
  {
    REQUIRE(util::write_file("ccache.conf", "no equal sign"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: missing equal sign");
  }

  SUBCASE("unknown key")
  {
    REQUIRE(util::write_file("ccache.conf", "# Comment\nfoo = bar"));
    CHECK(config.update_from_file("ccache.conf"));
  }

  SUBCASE("invalid bool")
  {
    REQUIRE(util::write_file("ccache.conf", "disable="));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: not a boolean value: \"\"");

    REQUIRE(util::write_file("ccache.conf", "disable=foo"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: not a boolean value: \"foo\"");
  }

  SUBCASE("invalid variable reference")
  {
    REQUIRE(util::write_file("ccache.conf", "base_dir = ${foo"));
    REQUIRE_THROWS_WITH(
      config.update_from_file("ccache.conf"),
      "ccache.conf:1: syntax error: missing '}' after \"foo\"");
    // Other cases tested in test_Util.c.
  }

  SUBCASE("empty umask")
  {
    REQUIRE(util::write_file("ccache.conf", "umask = "));
    CHECK(config.update_from_file("ccache.conf"));
    CHECK(config.umask() == std::nullopt);
  }

  SUBCASE("invalid size")
  {
    REQUIRE(util::write_file("ccache.conf", "max_size = foo"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: invalid size: \"foo\"");
    // Other cases tested in test_Util.c.
  }

  SUBCASE("unknown sloppiness")
  {
    REQUIRE(util::write_file("ccache.conf", "sloppiness = time_macros, foo"));
    CHECK(config.update_from_file("ccache.conf"));
    CHECK(config.sloppiness().to_bitmask()
          == static_cast<uint32_t>(core::Sloppy::time_macros));
  }

  SUBCASE("invalid unsigned")
  {
    REQUIRE(util::write_file("ccache.conf", "max_files ="));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: invalid unsigned integer: \"\"");

    REQUIRE(util::write_file("ccache.conf", "max_files = -42"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: invalid unsigned integer: \"-42\"");

    REQUIRE(util::write_file("ccache.conf", "max_files = foo"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: invalid unsigned integer: \"foo\"");
  }

  SUBCASE("missing file")
  {
    CHECK(!config.update_from_file("ccache.conf"));
  }

  SUBCASE("relative base dir")
  {
    REQUIRE(util::write_file("ccache.conf", "base_dir = relative"));
    REQUIRE_THROWS_WITH(config.update_from_file("ccache.conf"),
                        "ccache.conf:1: not an absolute path: \"relative\"");

    REQUIRE(util::write_file("ccache.conf", "base_dir ="));
    CHECK(config.update_from_file("ccache.conf"));
  }
}

TEST_CASE("Config::update_from_environment")
{
  Config config;

  util::setenv("CCACHE_COMPRESS", "1");
  config.update_from_environment();
  CHECK(config.compression());

  util::unsetenv("CCACHE_COMPRESS");

  util::setenv("CCACHE_NOCOMPRESS", "1");
  config.update_from_environment();
  CHECK(!config.compression());
}

TEST_CASE("Config::response_file_format")
{
  using ResponseFileFormat = util::Args::ResponseFileFormat;

  Config config;

  SUBCASE("from config gcc")
  {
    REQUIRE(util::write_file("ccache.conf", "response_file_format = posix"));
    CHECK(config.update_from_file("ccache.conf"));

    CHECK(config.response_file_format() == ResponseFileFormat::posix);
  }

  SUBCASE("from config msvc")
  {
    REQUIRE(util::write_file("ccache.conf", "response_file_format = windows"));
    CHECK(config.update_from_file("ccache.conf"));

    CHECK(config.response_file_format() == ResponseFileFormat::windows);
  }

  SUBCASE("from config msvc with clang compiler")
  {
    REQUIRE(util::write_file(
      "ccache.conf", "response_file_format = windows\ncompiler_type = clang"));
    CHECK(config.update_from_file("ccache.conf"));

    CHECK(config.response_file_format() == ResponseFileFormat::windows);
  }

  SUBCASE("guess from compiler gcc")
  {
    REQUIRE(util::write_file("ccache.conf", "compiler_type = clang"));
    CHECK(config.update_from_file("ccache.conf"));

    CHECK(config.response_file_format() == ResponseFileFormat::posix);
  }

  SUBCASE("guess from compiler msvc")
  {
    REQUIRE(util::write_file("ccache.conf", "compiler_type = msvc"));
    CHECK(config.update_from_file("ccache.conf"));

    CHECK(config.response_file_format() == ResponseFileFormat::windows);
  }
}

TEST_CASE("Config::set_value_in_file")
{
  TestContext test_context;
  Config config;

  SUBCASE("set new value")
  {
    REQUIRE(util::write_file("ccache.conf", "path = vanilla\n"));
    config.set_value_in_file("ccache.conf", "compiler", "chocolate");
    std::string content = *util::read_file<std::string>("ccache.conf");
    CHECK(content == "path = vanilla\ncompiler = chocolate\n");
  }

  SUBCASE("existing value")
  {
    REQUIRE(
      util::write_file("ccache.conf", "path = chocolate\nstats = chocolate\n"));
    config.set_value_in_file("ccache.conf", "path", "vanilla");
    std::string content = *util::read_file<std::string>("ccache.conf");
    CHECK(content == "path = vanilla\nstats = chocolate\n");
  }

  SUBCASE("unknown option")
  {
    REQUIRE(
      util::write_file("ccache.conf", "path = chocolate\nstats = chocolate\n"));
    try {
      config.set_value_in_file("ccache.conf", "foo", "bar");
      CHECK(false);
    } catch (const core::Error& e) {
      CHECK(std::string(e.what()) == "unknown configuration option \"foo\"");
    }

    std::string content = *util::read_file<std::string>("ccache.conf");
    CHECK(content == "path = chocolate\nstats = chocolate\n");
  }

  SUBCASE("unknown sloppiness")
  {
    REQUIRE(util::write_file("ccache.conf", "path = vanilla\n"));
    config.set_value_in_file("ccache.conf", "sloppiness", "foo");
    std::string content = *util::read_file<std::string>("ccache.conf");
    CHECK(content == "path = vanilla\nsloppiness = foo\n");
  }

  SUBCASE("comments are kept")
  {
    REQUIRE(util::write_file("ccache.conf", "# c1\npath = blueberry\n#c2\n"));
    config.set_value_in_file("ccache.conf", "path", "vanilla");
    config.set_value_in_file("ccache.conf", "compiler", "chocolate");
    std::string content = *util::read_file<std::string>("ccache.conf");
    CHECK(content == "# c1\npath = vanilla\n#c2\ncompiler = chocolate\n");
  }
}

TEST_CASE("Config::get_string_value")
{
  Config config;

  SUBCASE("base case")
  {
    config.set_max_files(42);
    CHECK(config.get_string_value("max_files") == "42");
  }

  SUBCASE("unknown key")
  {
    try {
      config.get_string_value("foo");
      CHECK(false);
    } catch (const core::Error& e) {
      CHECK(std::string(e.what()) == "unknown configuration option \"foo\"");
    }
  }
}

TEST_CASE("Config::visit_items")
{
  TestContext test_context;

#ifndef _WIN32
#  define BASE_DIR "/bd\n"
#else
#  define BASE_DIR "C:\\bd\n"
#endif

  REQUIRE(util::write_file(
    "test.conf",
    "absolute_paths_in_stderr = true\n"
    "base_dir = " BASE_DIR
    "cache_dir = cd\n"
    "compiler = c\n"
    "compiler_check = cc\n"
    "compiler_type = clang\n"
    "compression = true\n"
    "compression_level = 8\n"
    "cpp_extension = ce\n"
    "debug = false\n"
    "debug_dir = /dd\n"
    "debug_level = 2\n"
    "depend_mode = true\n"
    "direct_mode = false\n"
    "disable = true\n"
    "extra_files_to_hash = efth\n"
    "file_clone = true\n"
    "hard_link = true\n"
    "hash_dir = false\n"
    "ignore_headers_in_manifest = ihim\n"
    "ignore_options = -a=* -b\n"
    "inode_cache = false\n"
    "keep_comments_cpp = true\n"
    "log_file = lf\n"
    "max_files = 4711\n"
    "max_size = 98.7M\n"
    "msvc_dep_prefix = mdp\n"
    "namespace = ns\n"
    "path = p\n"
    "pch_external_checksum = true\n"
    "prefix_command = pc\n"
    "prefix_command_cpp = pcc\n"
    "read_only = true\n"
    "read_only_direct = true\n"
    "recache = true\n"
    "remote_only = true\n"
    "remote_storage = rs\n"
    "reshare = true\n"
    "response_file_format = posix\n"
    "sloppiness = include_file_mtime, include_file_ctime, time_macros,"
    " file_stat_matches, file_stat_matches_ctime, pch_defines, system_headers,"
    " clang_index_store, ivfsoverlay, gcno_cwd \n"
    "stats = false\n"
    "stats_log = sl\n"
    "temporary_dir = td\n"
    "umask = 022\n"));
#undef BASE_DIR

  Config config;
  config.update_from_file("test.conf");

  std::vector<std::string> received_items;

  config.visit_items(
    [&](const auto& key, const auto& value, const auto& origin) {
      received_items.push_back(FMT("({}) {} = {}", origin, key, value));
    });

  std::vector<std::string> expected = {
    "(test.conf) absolute_paths_in_stderr = true",
#ifndef _WIN32
    "(test.conf) base_dir = /bd",
#else
    "(test.conf) base_dir = C:\\bd",
#endif
    "(test.conf) cache_dir = cd",
    "(test.conf) compiler = c",
    "(test.conf) compiler_check = cc",
    "(test.conf) compiler_type = clang",
    "(test.conf) compression = true",
    "(test.conf) compression_level = 8",
    "(test.conf) cpp_extension = ce",
    "(test.conf) debug = false",
    "(test.conf) debug_dir = /dd",
    "(test.conf) debug_level = 2",
    "(test.conf) depend_mode = true",
    "(test.conf) direct_mode = false",
    "(test.conf) disable = true",
    "(test.conf) extra_files_to_hash = efth",
    "(test.conf) file_clone = true",
    "(test.conf) hard_link = true",
    "(test.conf) hash_dir = false",
    "(test.conf) ignore_headers_in_manifest = ihim",
    "(test.conf) ignore_options = -a=* -b",
    "(test.conf) inode_cache = false",
    "(test.conf) keep_comments_cpp = true",
    "(test.conf) log_file = lf",
    "(test.conf) max_files = 4711",
    "(test.conf) max_size = 98.7 MB",
    "(test.conf) msvc_dep_prefix = mdp",
    "(test.conf) namespace = ns",
    "(test.conf) path = p",
    "(test.conf) pch_external_checksum = true",
    "(test.conf) prefix_command = pc",
    "(test.conf) prefix_command_cpp = pcc",
    "(test.conf) read_only = true",
    "(test.conf) read_only_direct = true",
    "(test.conf) recache = true",
    "(test.conf) remote_only = true",
    "(test.conf) remote_storage = rs",
    "(test.conf) reshare = true",
    "(test.conf) response_file_format = posix",
    "(test.conf) sloppiness = clang_index_store, file_stat_matches,"
    " file_stat_matches_ctime, gcno_cwd, include_file_ctime,"
    " include_file_mtime, ivfsoverlay, pch_defines, system_headers,"
    " time_macros",
    "(test.conf) stats = false",
    "(test.conf) stats_log = sl",
    "(test.conf) temporary_dir = td",
    "(test.conf) umask = 022",
  };

  REQUIRE(received_items.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(received_items[i] == expected[i]);
  }
}

TEST_CASE("Check key tables consistency")
{
  CHECK_NOTHROW(Config::check_key_tables_consistency());
}
