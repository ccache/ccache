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

#include "Context.hpp"
#include "TestUtil.hpp"
#include "ccache.hpp"

#include "third_party/doctest.h"
#include "third_party/nonstd/optional.hpp"

#ifdef MYNAME
#  define CCACHE_NAME MYNAME
#else
#  define CCACHE_NAME "ccache"
#endif

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
    // relative or absolute path. If ccache *and* compiler have a relative or
    // absolute path, call ccache from PATH.
    CHECK_THROWS(helper(CCACHE_NAME " gcc", "", CCACHE_NAME));
    CHECK(helper(CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "", CCACHE_NAME) == "/abs/gcc");

    CHECK(helper("rel/" CCACHE_NAME " gcc", "", CCACHE_NAME) == "ccache");
    CHECK(helper("rel/" CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper("rel/" CCACHE_NAME " /a/gcc", "", CCACHE_NAME) == "/a/gcc");

    CHECK(helper("/abs/" CCACHE_NAME " gcc", "", CCACHE_NAME) == "ccache");
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

  SUBCASE("config")
  {
    // In case the first parameter is gcc it must be a link to ccache so use
    // config value instead.
    CHECK(helper("gcc", "config") == "resolved_config");
    CHECK(helper("gcc", "rel/config") == "resolved_rel/config");
    CHECK(helper("gcc", "/abs/config") == "resolved_/abs/config");
    CHECK(helper("rel/gcc", "config") == "resolved_config");
    CHECK(helper("rel/gcc", "rel/config") == "resolved_rel/config");
    CHECK(helper("rel/gcc", "/abs/config") == "resolved_/abs/config");
    CHECK(helper("/abs/gcc", "config") == "resolved_config");
    CHECK(helper("/abs/gcc", "rel/config") == "resolved_rel/config");
    CHECK(helper("/abs/gcc", "/abs/config") == "resolved_/abs/config");

    // In case the first parameter is ccache, use the configuration value unless
    // the second parameter is a relative or absolute path.
    CHECK(helper(CCACHE_NAME " gcc", "config") == "resolved_config");
    CHECK(helper(CCACHE_NAME " gcc", "rel/config") == "resolved_rel/config");
    CHECK(helper(CCACHE_NAME " gcc", "/abs/config") == "resolved_/abs/config");
    CHECK(helper(CCACHE_NAME " rel/gcc", "config") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "config") == "/abs/gcc");

    // Same as above with relative path to ccache.
    CHECK(helper("r/" CCACHE_NAME " gcc", "conf") == "resolved_conf");
    CHECK(helper("r/" CCACHE_NAME " gcc", "rel/conf") == "resolved_rel/conf");
    CHECK(helper("r/" CCACHE_NAME " gcc", "/abs/conf") == "resolved_/abs/conf");
    CHECK(helper("r/" CCACHE_NAME " rel/gcc", "conf") == "rel/gcc");
    CHECK(helper("r/" CCACHE_NAME " /abs/gcc", "conf") == "/abs/gcc");

    // Same as above with absolute path to ccache.
    CHECK(helper("/a/" CCACHE_NAME " gcc", "conf") == "resolved_conf");
    CHECK(helper("/a/" CCACHE_NAME " gcc", "rel/conf") == "resolved_rel/conf");
    CHECK(helper("/a/" CCACHE_NAME " gcc", "/a/conf") == "resolved_/a/conf");
    CHECK(helper("/a/" CCACHE_NAME " rel/gcc", "conf") == "rel/gcc");
    CHECK(helper("/a/" CCACHE_NAME " /abs/gcc", "conf") == "/abs/gcc");
  }
}

TEST_SUITE_END();
