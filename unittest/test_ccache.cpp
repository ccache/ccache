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

#include "third_party/doctest.h"
#include "third_party/nonstd/optional.hpp"

// Function under test
void find_compiler(Context& ctx,
                   const std::function<std::string(
                     const Context&, const std::string&, const std::string&)>&);

#ifdef MYNAME
#  define CCACHE_NAME MYNAME
#else
#  define CCACHE_NAME "ccache"
#endif

TEST_SUITE_BEGIN("ccache");

using TestUtil::TestContext;

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
    // find_compiler shall call find_executable to locate the next best "gcc"
    // and return that value.
    CHECK(helper("gcc", "") == "resolved_gcc");
    CHECK(helper("relative/gcc", "") == "resolved_gcc");
    CHECK(helper("/absolute/gcc", "") == "resolved_gcc");

    // In case the first param is ccache, resolve the second param to the real
    // compiler. Unless it's a relative/absolute path.
    CHECK(helper(CCACHE_NAME " gcc", "") == "resolved_gcc");
    CHECK(helper(CCACHE_NAME " rel/gcc", "") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "") == "/abs/gcc");

    // If gcc points back to ccache throw. Unless it's a relative/absolute path.
    CHECK_THROWS(helper(CCACHE_NAME " gcc", "", CCACHE_NAME));
    CHECK(helper(CCACHE_NAME " rel/gcc", "", CCACHE_NAME) == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "", CCACHE_NAME) == "/abs/gcc");

    // If compiler is not found throw. Unless it's a relative/absolute path.
    CHECK_THROWS(helper(CCACHE_NAME " gcc", "", ""));
    CHECK(helper(CCACHE_NAME " rel/gcc", "", "") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "", "") == "/abs/gcc");
  }

  SUBCASE("config")
  {
    // In case the first parameter is gcc it must be a link to ccache, use
    // config value instead. Assume config value to be base name only.
    CHECK(helper("gcc", "config") == "resolved_config");
    CHECK(helper("relative/gcc", "config") == "resolved_config");
    CHECK(helper("/absolute/gcc", "config") == "resolved_config");
    CHECK(helper("gcc", "relative/config") == "resolved_relative/config");
    CHECK(helper("gcc", "/absolute/config") == "resolved_/absolute/config");

    // In case the first param is ccache, use config value. Unless it's a
    // relative/absolute path. Assume config value to be base name only.
    CHECK(helper(CCACHE_NAME " gcc", "config") == "resolved_config");
    CHECK(helper(CCACHE_NAME " gcc", "rel/config") == "resolved_rel/config");
    CHECK(helper(CCACHE_NAME " gcc", "/abs/config") == "resolved_/abs/config");
    CHECK(helper(CCACHE_NAME " rel/gcc", "config") == "rel/gcc");
    CHECK(helper(CCACHE_NAME " /abs/gcc", "config") == "/abs/gcc");
  }
}

TEST_SUITE_END();
