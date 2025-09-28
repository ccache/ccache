// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include <ccache/ccache.hpp>
#include <ccache/context.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace fs = util::filesystem;
using TestUtil::TestContext;
using util::Args;

TEST_SUITE_BEGIN("ccache");

// Wraps find_compiler in a test friendly interface.
static std::string
helper(bool masquerading_as_compiler,
       const char* args,
       const char* config_compiler,
       const char* find_executable_return_string = nullptr)
{
  const auto find_executable_stub =
    [&find_executable_return_string](const auto&, const auto& s, const auto&) {
      return find_executable_return_string ? find_executable_return_string
                                           : "resolved_" + s;
    };

  Context ctx;
  ctx.config.set_compiler(config_compiler);
  ctx.orig_args = Args::from_string(args);
  find_compiler(ctx, find_executable_stub, masquerading_as_compiler);
  return ctx.orig_args.to_string();
}

TEST_CASE("split_argv")
{
  ArgvParts argv_parts;

  SUBCASE("empty")
  {
    argv_parts = split_argv(0, nullptr);
    CHECK(argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("ccache")
  {
    const char* const argv[] = {"ccache"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("normal compilation")
  {
    const char* const argv[] = {"ccache", "gcc", "-c", "test.c"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args == Args::from_string("gcc -c test.c"));
  }

  SUBCASE("only config options")
  {
    const char* const argv[] = {"ccache", "foo=bar"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings == std::vector<std::string>{"foo=bar"});
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("compilation with config options")
  {
    const char* const argv[] = {"ccache", "a=b", "c = d", "/usr/bin/gcc"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings
          == std::vector<std::string>{"a=b", "c = d"});
    CHECK(argv_parts.compiler_and_args == Args::from_string("/usr/bin/gcc"));
  }

  SUBCASE("compilation with config option ending with /ccache")
  {
    const char* const argv[] = {"ccache", "a=b/ccache", "/usr/bin/gcc"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings == std::vector<std::string>{"a=b/ccache"});
    CHECK(argv_parts.compiler_and_args == Args::from_string("/usr/bin/gcc"));
  }
}

TEST_CASE("find_compiler")
{
  SUBCASE("no config")
  {
    // In case the first parameter is gcc it must be a link to ccache, so
    // find_compiler should call find_executable to locate the next best "gcc"
    // and return that value.
    CHECK(helper(true, "gcc", "") == "resolved_gcc");
    CHECK(helper(true, "relative/gcc", "") == "resolved_gcc");
    CHECK(helper(true, "/absolute/gcc", "") == "resolved_gcc");

    // In case the first parameter is ccache, resolve the second parameter to
    // the real compiler unless it's a relative or absolute path.
    CHECK(helper(false, "gcc", "") == "resolved_gcc");
    CHECK(helper(false, "rel/gcc", "") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "") == "/abs/gcc");

    // If gcc points back to ccache throw, unless either ccache or gcc is a
    // relative or absolute path.
    CHECK_THROWS(helper(false, "gcc", "", "ccache"));
    CHECK(helper(false, "rel/gcc", "", "ccache") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "", "ccache") == "/abs/gcc");

    // If compiler is not found then throw, unless the compiler has a relative
    // or absolute path.
    CHECK_THROWS(helper(false, "gcc", "", ""));
    CHECK(helper(false, "rel/gcc", "", "") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "", "") == "/abs/gcc");
  }

  SUBCASE("config")
  {
    // In case the first parameter is gcc it must be a link to ccache so use
    // config value instead. Don't resolve config if it's a relative or absolute
    // path.
    CHECK(helper(true, "gcc", "config") == "resolved_config");
    CHECK(helper(true, "gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "gcc", "/abs/config") == "/abs/config");
    CHECK(helper(true, "rel/gcc", "config") == "resolved_config");
    CHECK(helper(true, "rel/gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "rel/gcc", "/abs/config") == "/abs/config");
    CHECK(helper(true, "/abs/gcc", "config") == "resolved_config");
    CHECK(helper(true, "/abs/gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "/abs/gcc", "/abs/config") == "/abs/config");

    // In case the first parameter is ccache, use the configuration value. Don't
    // resolve configuration value if it's a relative or absolute path.
    CHECK(helper(false, "gcc", "config") == "resolved_config");
    CHECK(helper(false, "gcc", "rel/config") == "rel/config");
    CHECK(helper(false, "gcc", "/abs/config") == "/abs/config");
    CHECK(helper(false, "rel/gcc", "config") == "resolved_config");
    CHECK(helper(false, "/abs/gcc", "config") == "resolved_config");
  }
}

TEST_CASE("guess_compiler")
{
  TestContext test_context;

  SUBCASE("Compiler not in file system")
  {
    CHECK(guess_compiler("/test/prefix/clang") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang-3.8") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang++") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang++-10") == CompilerType::clang);

    CHECK(guess_compiler("/test/prefix/gcc") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/gcc-4.8") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/g++") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/g++-9") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/x86_64-w64-mingw32-gcc-posix")
          == CompilerType::gcc);

    CHECK(guess_compiler("/test/prefix/nvcc") == CompilerType::nvcc);
    CHECK(guess_compiler("/test/prefix/nvcc-10.1.243") == CompilerType::nvcc);

    CHECK(guess_compiler("/test/prefix/x") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/cc") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/c++") == CompilerType::other);
  }

#ifndef _WIN32
  SUBCASE("Follow symlink to actual compiler")
  {
    const auto cwd = *fs::current_path();
    REQUIRE(util::write_file(cwd / "gcc", ""));
    CHECK(fs::create_symlink("gcc", cwd / "intermediate"));
    const auto cc = cwd / "cc";
    CHECK(fs::create_symlink("intermediate", cc));

    CHECK(guess_compiler(cc) == CompilerType::gcc);
  }

  SUBCASE("Classify clang-cl symlink to clang")
  {
    const auto cwd = *fs::current_path();
    REQUIRE(util::write_file(cwd / "clang", ""));
    const auto clang_cl = cwd / "clang-cl";
    CHECK(fs::create_symlink("clang", clang_cl));

    CHECK(guess_compiler(clang_cl) == CompilerType::clang_cl);
  }

  SUBCASE("Probe hardlink for actual compiler, gcc")
  {
    const auto cwd = *fs::current_path();
    const auto cc = cwd / "cc";
    const auto gcc = cwd / "gcc";
    REQUIRE(util::write_file(cwd / "cc", ""));
    CHECK(fs::create_hard_link(cc, gcc));

    CHECK(guess_compiler(cc) == CompilerType::gcc);
  }

  SUBCASE("Probe hardlink for actual compiler, clang")
  {
    const auto cwd = *fs::current_path();
    const auto cc = cwd / "cc";
    const auto clang = cwd / "clang";
    REQUIRE(util::write_file(cwd / "cc", ""));
    CHECK(fs::create_hard_link(cc, clang));

    CHECK(guess_compiler(cc) == CompilerType::clang);
  }

  SUBCASE("Probe hardlink for actual compiler, gcc+clang")
  {
    const auto cwd = *fs::current_path();
    const auto cc = cwd / "cc";
    const auto gcc = cwd / "gcc";
    const auto clang = cwd / "clang";
    REQUIRE(util::write_file(cwd / "cc", ""));
    CHECK(fs::create_hard_link(cc, gcc));
    CHECK(fs::create_hard_link(cc, clang));

    CHECK(guess_compiler(cc) == CompilerType::clang);
  }
#endif
}

TEST_CASE("is_ccache_executable")
{
  CHECK(is_ccache_executable("ccache"));
  CHECK(is_ccache_executable("ccache-1.2.3"));
  CHECK(!is_ccache_executable("fooccache"));
  CHECK(!is_ccache_executable("gcc"));
#ifdef _WIN32
  CHECK(is_ccache_executable("CCACHE"));
  CHECK(is_ccache_executable("CCACHE.exe"));
  CHECK(is_ccache_executable("CCACHE-1.2.3"));
  CHECK(is_ccache_executable("CCACHE.EXE"));
  CHECK(is_ccache_executable("CCACHE-1.2.3.EXE"));
#endif
}

TEST_CASE("file_path_matches_dir_prefix_or_file")
{
  CHECK(file_path_matches_dir_prefix_or_file("aa", "aa"));
  CHECK(!file_path_matches_dir_prefix_or_file("aaa", "aa"));
  CHECK(!file_path_matches_dir_prefix_or_file("aa", "aaa"));
  CHECK(file_path_matches_dir_prefix_or_file("aa/", "aa"));

  CHECK(file_path_matches_dir_prefix_or_file("/aa/bb", "/aa/bb"));
  CHECK(!file_path_matches_dir_prefix_or_file("/aa/b", "/aa/bb"));
  CHECK(!file_path_matches_dir_prefix_or_file("/aa/bbb", "/aa/bb"));

  CHECK(file_path_matches_dir_prefix_or_file("/aa", "/aa/bb"));
  CHECK(file_path_matches_dir_prefix_or_file("/aa/", "/aa/bb"));
  CHECK(!file_path_matches_dir_prefix_or_file("/aa/bb", "/aa"));

#ifdef _WIN32
  CHECK(file_path_matches_dir_prefix_or_file("\\aa", "\\aa\\bb"));
  CHECK(file_path_matches_dir_prefix_or_file("\\aa\\", "\\aa\\bb"));
#else
  CHECK(!file_path_matches_dir_prefix_or_file("\\aa", "\\aa\\bb"));
  CHECK(!file_path_matches_dir_prefix_or_file("\\aa\\", "\\aa\\bb"));
#endif
}

TEST_SUITE_END();
