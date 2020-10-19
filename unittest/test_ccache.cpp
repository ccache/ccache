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
    // e.g. due to some suboptimal setup, scripts etc.
    // Source: https://github.com/ccache/ccache/issues/686
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

TEST_CASE("rewrite_dep_file_paths")
{
  Context ctx;

  const auto cwd = ctx.actual_cwd;
  ctx.has_absolute_include_headers = true;

  const auto content =
    fmt::format("foo.o: bar.c {0}/bar.h \\\n {1}/fie.h {0}/fum.h\n",
                cwd,
                Util::dir_name(cwd));

  SUBCASE("Base directory not in dep file content")
  {
    ctx.config.set_base_dir("/foo/bar");
    CHECK(!rewrite_dep_file_paths(ctx, ""));
    CHECK(!rewrite_dep_file_paths(ctx, content));
  }

  SUBCASE("Base directory in dep file content but not matching")
  {
    ctx.config.set_base_dir(fmt::format("{}/other", Util::dir_name(cwd)));
    CHECK(!rewrite_dep_file_paths(ctx, ""));
    CHECK(!rewrite_dep_file_paths(ctx, content));
  }

  SUBCASE("Absolute paths under base directory rewritten")
  {
    ctx.config.set_base_dir(cwd);
    const auto actual = rewrite_dep_file_paths(ctx, content);
    const auto expected = fmt::format(
      "foo.o: bar.c ./bar.h \\\n {}/fie.h ./fum.h\n", Util::dir_name(cwd));
    REQUIRE(actual);
    CHECK(*actual == expected);
  }
}

TEST_SUITE_END();
