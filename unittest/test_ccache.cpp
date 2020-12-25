// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "../src/Context.hpp"
#include "../src/ccache.hpp"
#include "TestUtil.hpp"
#include "core/fmtmacros.hpp"

#include "third_party/doctest.h"
#include "third_party/nonstd/optional.hpp"

#ifdef MYNAME
#  define CCACHE_NAME MYNAME
#else
#  define CCACHE_NAME "ccache"
#endif

using TestUtil::TestContext;

TEST_SUITE_BEGIN("ccache");

// Wraps find_compiler in a test friendly interface.
static std::string
helper(const char* args,
       const char* config_compiler,
       const char* find_executable_return_string = nullptr)
{
  const auto find_executable_stub =
    [&find_executable_return_string](
      const Context&, const std::string& s, const std::string&) -> std::string {
    return find_executable_return_string ? find_executable_return_string
                                         : "resolved_" + s;
  };

  Context ctx;
  ctx.config.set_compiler(config_compiler);
  ctx.orig_args = Args::from_string(args);
  find_compiler(ctx, find_executable_stub);
  return ctx.orig_args.to_string();
}

TEST_CASE("find_compiler")
{
  SUBCASE("no config")
  {
    // In case the first parameter is gcc it must be a link to ccache, so
    // find_compiler should call find_executable to locate the next best "gcc"
    // and return that value.
    CHECK(helper("gcc", "") == "resolved_gcc");
    CHECK(helper("relative/gcc", "") == "resolved_gcc");
    CHECK(helper("/absolute/gcc", "") == "resolved_gcc");

    // In case the first parameter is ccache, resolve the second parameter to
    // the real compiler unless it's a relative or absolute path.
    CHECK(helper(CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper(CCACHE_NAME " rel/gcc", "") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "") == "/abs/gcc");

    CHECK(helper("rel/" CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper("rel/" CCACHE_NAME " rel/gcc", "") == "rel/gcc");
    CHECK(helper("rel/" CCACHE_NAME " /abs/gcc", "") == "/abs/gcc");

    CHECK(helper("/abs/" CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper("/abs/" CCACHE_NAME " rel/gcc", "") == "rel/gcc");
    CHECK(helper("/abs/" CCACHE_NAME " /abs/gcc", "") == "/abs/gcc");

    // If gcc points back to ccache throw, unless either ccache or gcc is a
    // relative or absolute path.
    CHECK_THROWS(helper(CCACHE_NAME " gcc", "", CCACHE_NAME));
    CHECK(helper(CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "", CCACHE_NAME) == "/abs/gcc");

    CHECK_THROWS(helper("rel/" CCACHE_NAME " gcc", "", CCACHE_NAME));
    CHECK(helper("rel/" CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper("rel/" CCACHE_NAME " /a/gcc", "", CCACHE_NAME) == "/a/gcc");

    CHECK_THROWS(helper("/abs/" CCACHE_NAME " gcc", "", CCACHE_NAME));
    CHECK(helper("/abs/" CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper("/abs/" CCACHE_NAME " /a/gcc", "", CCACHE_NAME) == "/a/gcc");

    // If compiler is not found then throw, unless the compiler has a relative
    // or absolute path.
    CHECK_THROWS(helper(CCACHE_NAME " gcc", "", ""));
    CHECK(helper(CCACHE_NAME " rel/gcc", "", "") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "", "") == "/abs/gcc");

    CHECK_THROWS(helper("rel/" CCACHE_NAME " gcc", "", ""));
    CHECK(helper("rel/" CCACHE_NAME " rel/gcc", "", "") == "rel/gcc");
    CHECK(helper("rel/" CCACHE_NAME " /abs/gcc", "", "") == "/abs/gcc");

    CHECK_THROWS(helper("/abs/" CCACHE_NAME " gcc", "", ""));
    CHECK(helper("/abs/" CCACHE_NAME " rel/gcc", "", "") == "rel/gcc");
    CHECK(helper("/abs/" CCACHE_NAME " /abs/gcc", "", "") == "/abs/gcc");
  }

  SUBCASE("double ccache")
  {
    // E.g. due to some suboptimal setup, scripts etc. Source:
    // https://github.com/ccache/ccache/issues/686
    CHECK(helper(CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper(CCACHE_NAME " " CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper(CCACHE_NAME " " CCACHE_NAME " " CCACHE_NAME " gcc", "")
          == "resolved_gcc");
  }

  SUBCASE("config")
  {
    // In case the first parameter is gcc it must be a link to ccache so use
    // config value instead. Don't resolve config if it's a relative or absolute
    // path.
    CHECK(helper("gcc", "config") == "resolved_config");
    CHECK(helper("gcc", "rel/config") == "rel/config");
    CHECK(helper("gcc", "/abs/config") == "/abs/config");
    CHECK(helper("rel/gcc", "config") == "resolved_config");
    CHECK(helper("rel/gcc", "rel/config") == "rel/config");
    CHECK(helper("rel/gcc", "/abs/config") == "/abs/config");
    CHECK(helper("/abs/gcc", "config") == "resolved_config");
    CHECK(helper("/abs/gcc", "rel/config") == "rel/config");
    CHECK(helper("/abs/gcc", "/abs/config") == "/abs/config");

    // In case the first parameter is ccache, use the configuration value. Don't
    // resolve configuration value if it's a relative or absolute path.
    CHECK(helper(CCACHE_NAME " gcc", "config") == "resolved_config");
    CHECK(helper(CCACHE_NAME " gcc", "rel/config") == "rel/config");
    CHECK(helper(CCACHE_NAME " gcc", "/abs/config") == "/abs/config");
    CHECK(helper(CCACHE_NAME " rel/gcc", "config") == "resolved_config");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "config") == "resolved_config");

    // Same as above with relative path to ccache.
    CHECK(helper("r/" CCACHE_NAME " gcc", "conf") == "resolved_conf");
    CHECK(helper("r/" CCACHE_NAME " gcc", "rel/conf") == "rel/conf");
    CHECK(helper("r/" CCACHE_NAME " gcc", "/abs/conf") == "/abs/conf");
    CHECK(helper("r/" CCACHE_NAME " rel/gcc", "conf") == "resolved_conf");
    CHECK(helper("r/" CCACHE_NAME " /abs/gcc", "conf") == "resolved_conf");

    // Same as above with absolute path to ccache.
    CHECK(helper("/a/" CCACHE_NAME " gcc", "conf") == "resolved_conf");
    CHECK(helper("/a/" CCACHE_NAME " gcc", "rel/conf") == "rel/conf");
    CHECK(helper("/a/" CCACHE_NAME " gcc", "/a/conf") == "/a/conf");
    CHECK(helper("/a/" CCACHE_NAME " rel/gcc", "conf") == "resolved_conf");
    CHECK(helper("/a/" CCACHE_NAME " /abs/gcc", "conf") == "resolved_conf");
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

    CHECK(guess_compiler("/test/prefix/pump") == CompilerType::pump);
    CHECK(guess_compiler("/test/prefix/distcc-pump") == CompilerType::pump);

    CHECK(guess_compiler("/test/prefix/x") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/cc") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/c++") == CompilerType::other);
  }

#ifndef _WIN32
  SUBCASE("Follow symlink to actual compiler")
  {
    const auto cwd = Util::get_actual_cwd();
    Util::write_file(FMT("{}/gcc", cwd), "");
    CHECK(symlink("gcc", FMT("{}/intermediate", cwd).c_str()) == 0);
    const auto cc = FMT("{}/cc", cwd);
    CHECK(symlink("intermediate", cc.c_str()) == 0);

    CHECK(guess_compiler(cc) == CompilerType::gcc);
  }
#endif
}

TEST_SUITE_END();
