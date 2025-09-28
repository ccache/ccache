// Copyright (C) 2010-2025 Joel Rosdahl and other contributors
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

#include <ccache/argprocessing.hpp>
#include <ccache/config.hpp>
#include <ccache/context.hpp>
#include <ccache/core/statistic.hpp>
#include <ccache/util/args.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#include <algorithm>

namespace fs = util::filesystem;

using core::Statistic;
using TestUtil::TestContext;
using util::Args;

namespace {

fs::path
get_root()
{
  auto cwd = fs::current_path();
  if (!cwd) {
    FAIL("get_root failed: ", cwd.error());
  }
  return cwd->root_path();
}

} // namespace

TEST_SUITE_BEGIN("argprocessing");

TEST_CASE("pass -fsyntax-only to compiler only")
{
  TestContext test_context;
  Context ctx;

  ctx.orig_args = Args::from_string("cc -c foo.c -fsyntax-only");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-fsyntax-only");
  CHECK(result->compiler_args.to_string() == "cc -fsyntax-only -c");
}

TEST_CASE("dash_E_should_result_in_called_for_preprocessing")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args = Args::from_string("cc -c foo.c -E");

  REQUIRE(util::write_file("foo.c", ""));
  CHECK(process_args(ctx).error() == Statistic::called_for_preprocessing);
}

TEST_CASE("dash_M_should_be_unsupported")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args = Args::from_string("cc -c foo.c -M");

  REQUIRE(util::write_file("foo.c", ""));
  CHECK(process_args(ctx).error() == Statistic::unsupported_compiler_option);
}

TEST_CASE("dependency_args_to_compiler")
{
  TestContext test_context;
  const std::string dep_args =
    "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MP"
    " -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf";
  Context ctx;
  ctx.orig_args = Args::from_string("cc " + dep_args + " -c foo.c -o foo.o");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string() == "cc " + dep_args + " -c");
}

TEST_CASE("cpp_only_args_to_preprocessor_and_compiler")
{
  TestContext test_context;
  const std::string cpp_args =
    "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"
    " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."
    " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."
    " -trigraphs -fworking-directory -fno-working-directory";
  const std::string dep_args =
    "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2";
  Context ctx;
  ctx.orig_args =
    Args::from_string("cc " + cpp_args + " " + dep_args + " -c foo.c -o foo.o");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc " + cpp_args);
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string()
        == "cc " + cpp_args + " " + dep_args + " -c");
}

TEST_CASE(
  "dependency_args_that_take_an_argument_should_not_require_space_delimiter")
{
  TestContext test_context;
  const std::string dep_args = "-MMD -MFfoo.d -MT mt -MTmt -MQmq";
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c " + dep_args + " foo.c -o foo.o");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string() == "cc " + dep_args + " -c");
}

TEST_CASE("equal_sign_after_MF_should_be_removed")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MF=path foo.c -o foo.o");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MFpath");
  CHECK(result->compiler_args.to_string() == "cc -MFpath -c");
}

TEST_CASE("sysroot_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    FMT("cc --sysroot={}/foo/bar -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
#ifdef _WIN32
  CHECK(result->preprocessor_args[1] == "--sysroot=foo\\bar");
#else
  CHECK(result->preprocessor_args[1] == "--sysroot=foo/bar");
#endif
}

TEST_CASE(
  "sysroot_with_separate_argument_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string = FMT("cc --sysroot {}/foo -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args[1] == "--sysroot");
  CHECK(result->preprocessor_args[2] == "foo");
}

TEST_CASE("fbuild_session_file_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    FMT("cc -fbuild-session-file={}/foo/bar -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
#ifdef _WIN32
  CHECK(result->preprocessor_args[1] == "-fbuild-session-file=foo\\bar");
#else
  CHECK(result->preprocessor_args[1] == "-fbuild-session-file=foo/bar");
#endif
}

TEST_CASE(
  "ivfsoverlay_with_separate_argument_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;
  ctx.config.update_from_map({
    {"sloppiness", "ivfsoverlay"}
  });

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    FMT("cc -ivfsoverlay {}/foo -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args[1] == "-ivfsoverlay");
  CHECK(result->preprocessor_args[2] == "foo");
}

TEST_CASE(
  "fmodules_cache_path_with_separate_argument_should_be_rewritten_if_basedir_"
  "is_used")
{
  TestContext test_context;

  Context ctx;
  ctx.config.update_from_map({
    {"sloppiness", "modules"}
  });

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    FMT("cc -fmodules-cache-path={}/foo/bar -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
#ifdef _WIN32
  CHECK(result->preprocessor_args[1] == "-fmodules-cache-path=foo\\bar");
#else
  CHECK(result->preprocessor_args[1] == "-fmodules-cache-path=foo/bar");
#endif
}

TEST_CASE(
  "fmodules_map_file_with_separate_argument_should_be_rewritten_if_basedir_"
  "is_used")
{
  TestContext test_context;

  Context ctx;
  ctx.config.update_from_map({
    {"sloppiness", "modules"}
  });

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    FMT("cc -fmodule-map-file={}/foo/bar -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
#ifdef _WIN32
  CHECK(result->preprocessor_args[1] == "-fmodule-map-file=foo\\bar");
#else
  CHECK(result->preprocessor_args[1] == "-fmodule-map-file=foo/bar");
#endif
}

TEST_CASE("MF_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MT bar -MFfoo.d");

  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MMD -MT bar -MFfoo.d");
  CHECK(result->compiler_args.to_string() == "cc -MMD -MT bar -MFfoo.d -c");
}

TEST_CASE("MT_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MT foo -MTbar");

  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string()
        == "-MMD -MFfoo.d -MT foo -MTbar");
  CHECK(result->compiler_args.to_string()
        == "cc -MMD -MFfoo.d -MT foo -MTbar -c");
}

TEST_CASE("MQ_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MQ foo -MQbar");

  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string()
        == "-MMD -MFfoo.d -MQ foo -MQbar");
  CHECK(result->compiler_args.to_string()
        == "cc -MMD -MFfoo.d -MQ foo -MQbar -c");
}

TEST_CASE("MQ_flag_without_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d foo.c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MQ foo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -MD -MP -MFfoo.d -MQ foo.d -c");
}

TEST_CASE("MT_flag_without_immediate_argument_should_not_add_MTobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d foo.c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MT foo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -MD -MP -MFfoo.d -MT foo.d -c");
}

TEST_CASE("MQ_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d foo.c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MQfoo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -MD -MP -MFfoo.d -MQfoo.d -c");
}

TEST_CASE("MT_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d foo.c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MTfoo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -MD -MP -MFfoo.d -MTfoo.d -c");
}

TEST_CASE(
  "isystem_flag_with_separate_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir(get_root());
  std::string arg_string = FMT("cc -isystem {}/foo -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args[2] == "foo");
}

#ifndef _WIN32
TEST_CASE("isystem_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir("/");
  std::string cwd = ctx.actual_cwd;
  std::string arg_string = FMT("cc -isystem{}/foo -c foo.c", cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args[1] == "-isystemfoo");
}

TEST_CASE("I_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  REQUIRE(util::write_file("foo.c", ""));
  ctx.config.set_base_dir("/");
  std::string cwd = *fs::current_path();
  std::string arg_string = FMT("cc -I{}/foo -c foo.c", cwd);
  ctx.orig_args = Args::from_string(arg_string);

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args[1] == "-Ifoo");
}
#endif // _WIN32

TEST_CASE("debug_flag_order_with_known_option_first")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -g1 -gsplit-dwarf foo.c -c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -g1 -gsplit-dwarf");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -g1 -gsplit-dwarf -c");
}

TEST_CASE("debug_flag_order_with_known_option_last")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -gsplit-dwarf -g1 foo.c -c");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -gsplit-dwarf -g1");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -gsplit-dwarf -g1 -c");
}

TEST_CASE("options_not_to_be_passed_to_the_preprocessor")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string(
    "cc -Wa,foo foo.c -g -c -DX -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -g -DX");
  CHECK(result->extra_args_to_hash.to_string()
        == "-Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  CHECK(result->compiler_args.to_string()
        == "cc -Wa,foo -g -DX -Werror -Xlinker fie -Xlinker,fum -Wno-error -c");
}

TEST_CASE("cuda_option_file")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::nvcc);
  ctx.orig_args = Args::from_string("nvcc -optf foo.optf,bar.optf");
  REQUIRE(util::write_file("foo.c", ""));
  REQUIRE(util::write_file("foo.optf", "-c foo.c -g -Wall -o"));
  REQUIRE(util::write_file("bar.optf", "out -DX"));

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "nvcc -g -Wall -DX");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "nvcc -g -Wall -DX -c");
}

TEST_CASE("nvcc_warning_flags_short")
{
  // With -Werror. This should conflict with host's -Werror flag.
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::nvcc);
  ctx.orig_args =
    Args::from_string("nvcc -Werror all-warnings -Xcompiler -Werror -c foo.cu");
  REQUIRE(util::write_file("foo.cu", ""));
  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "nvcc -Xcompiler -Werror");
  CHECK(result->extra_args_to_hash.to_string() == "-Werror all-warnings");
  CHECK(result->compiler_args.to_string()
        == "nvcc -Werror all-warnings -Xcompiler -Werror -c");
}

TEST_CASE("nvcc_warning_flags_long")
{
  // With --Werror. This shouldn't conflict with host's -Werror flag.
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::nvcc);
  ctx.orig_args = Args::from_string(
    "nvcc --Werror all-warnings -Xcompiler -Werror -c foo.cu");
  REQUIRE(util::write_file("foo.cu", ""));
  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "nvcc -Xcompiler -Werror");
  CHECK(result->extra_args_to_hash.to_string() == "--Werror all-warnings");
  CHECK(result->compiler_args.to_string()
        == "nvcc --Werror all-warnings -Xcompiler -Werror -c");
}

TEST_CASE("-Xclang")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::clang);

  const std::string common_args =
    "-Xclang -fno-pch-timestamp"
    " -Xclang unsupported";

  const std::string color_diag = "-Xclang -fcolor-diagnostics";

  const std::string extra_args =
    "-Xclang -emit-pch"
    " -Xclang -emit-pth";

  const std::string pch_pth_variants =
    "-Xclang -include-pch pch_path1"
    " -Xclang -include-pch -Xclang pch_path2"
    " -Xclang -include-pth pth_path1"
    " -Xclang -include-pth -Xclang pth_path2";

  ctx.orig_args =
    Args::from_string("clang -c foo.c " + common_args + " " + color_diag + " "
                      + extra_args + " " + pch_pth_variants);
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);
  CHECK(result->preprocessor_args.to_string()
        == "clang " + common_args + " " + pch_pth_variants);
  CHECK(result->extra_args_to_hash.to_string() == extra_args);
  CHECK(result->compiler_args.to_string()
        == "clang " + common_args + " " + color_diag + " " + extra_args + " "
             + pch_pth_variants + " -fcolor-diagnostics -c");
}

TEST_CASE("-x")
{
  TestContext test_context;
  Context ctx;
  REQUIRE(util::write_file("foo.c", ""));

  SUBCASE("intel option")
  {
    // -xCODE1 (where CODE1 can be e.g. Host or CORE-AVX2, always starting with
    // an uppercase letter) is an ordinary Intel compiler option, not a language
    // specification.
    ctx.orig_args = Args::from_string("gcc -c foo.c -xCODE");

    const auto result = process_args(ctx);
    CHECK(result->preprocessor_args.to_string() == "gcc -xCODE");
    CHECK(result->extra_args_to_hash.to_string() == "");
    CHECK(result->compiler_args.to_string() == "gcc -xCODE -c");
  }

  SUBCASE("compile .c as c++ (without space)")
  {
    ctx.orig_args = Args::from_string("gcc -xc++ -c foo.c");
    const auto result = process_args(ctx);
    CHECK(ctx.args_info.actual_language == "c++");
    CHECK(result->preprocessor_args.to_string() == "gcc -x c++");
    CHECK(result->extra_args_to_hash.to_string() == "");
    CHECK(result->compiler_args.to_string() == "gcc -x c++ -c");
  }

  SUBCASE("compile .c as c++ (with space)")
  {
    ctx.orig_args = Args::from_string("gcc -x c++ -c foo.c");
    const auto result = process_args(ctx);
    CHECK(ctx.args_info.actual_language == "c++");
    CHECK(result->preprocessor_args.to_string() == "gcc -x c++");
    CHECK(result->extra_args_to_hash.to_string() == "");
    CHECK(result->compiler_args.to_string() == "gcc -x c++ -c");
  }

  SUBCASE("compile .c as c++ (file first, no effect)")
  {
    ctx.orig_args = Args::from_string("gcc -c foo.c -x c++");
    const auto result = process_args(ctx);
    CHECK(ctx.args_info.actual_language == "c");
    CHECK(result->preprocessor_args.to_string() == "gcc");
    CHECK(result->extra_args_to_hash.to_string() == "");
    CHECK(result->compiler_args.to_string() == "gcc -c");
  }

  SUBCASE("unknown -x option (lowercase)")
  {
    ctx.orig_args = Args::from_string("gcc -x unsupported_language -c foo.c");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::unsupported_source_language);
    CHECK(ctx.args_info.actual_language == "");
  }

  SUBCASE("UNKNOWN -x option (uppercase)")
  {
    ctx.orig_args = Args::from_string("gcc -x UNSUPPORTED_LANGUAGE -c foo.c");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::unsupported_source_language);
    CHECK(ctx.args_info.actual_language == "");
  }

  SUBCASE("missing param")
  {
    ctx.orig_args = Args::from_string("gcc -c foo.c -x");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::bad_compiler_arguments);
    CHECK(ctx.args_info.actual_language == "");
  }
}

// On macOS ctx.actual_cwd typically starts with /Users which clashes with
// MSVC's /U option, so disable the test case there. This will be possible to
// improve when/if a compiler abstraction is introduced (issue #956).
TEST_CASE("MSVC options"
          * doctest::skip(util::starts_with(fs::current_path()->string(),
                                            "/U")))
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);

  REQUIRE(util::write_file("foo.c", ""));

  ctx.orig_args = Args::from_string(
    FMT("cl.exe /Fobar.obj /c {}/foo.c /foobar", ctx.actual_cwd));
  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cl.exe /foobar");
  CHECK(result->compiler_args.to_string() == "cl.exe /foobar /c");
}

TEST_CASE("MSVC PCH options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("foo.cpp", ""));
  REQUIRE(util::write_file("pch.h", ""));
  REQUIRE(util::write_file("pch.cpp", ""));

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Ycpch.h /Fppch.cpp.pch /FIpch.h /Fopch.cpp.obj /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.cpp.pch");
    CHECK(ctx.args_info.output_obj == "pch.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Ycpch.h /Fppch.cpp.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Ycpch.h /Fppch.cpp.pch /FIpch.h /c");
  }

  REQUIRE(util::write_file("pch.cpp.pch", ""));
  ctx.config.update_from_map({
    {"sloppiness", "pch_defines,time_macros"}
  });

  SUBCASE("Consume PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h /Fofoo.cpp.obj /c foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(!ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.cpp.pch");
    CHECK(ctx.args_info.output_obj == "foo.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h /c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("foo.cpp", ""));
  REQUIRE(util::write_file("pch.h", ""));
  REQUIRE(util::write_file("pch.cpp", ""));

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Yc /Fppch.cpp.pch /FIpch.h /Fopch.cpp.obj /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.cpp.pch");
    CHECK(ctx.args_info.output_obj == "pch.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Yc /Fppch.cpp.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Yc /Fppch.cpp.pch /FIpch.h /c");
  }

  REQUIRE(util::write_file("pch.cpp.pch", ""));
  ctx.config.update_from_map({
    {"sloppiness", "pch_defines,time_macros"}
  });

  SUBCASE("Consume PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h /Fofoo.cpp.obj /c foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(!ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.cpp.pch");
    CHECK(ctx.args_info.output_obj == "foo.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h /c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc and without -Fp")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("foo.cpp", ""));
  REQUIRE(util::write_file("pch.h", ""));
  REQUIRE(util::write_file("pch.cpp", ""));

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string("cl.exe /Yc /Fopch.cpp.obj /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "pch.cpp.obj");
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Yc");
    CHECK(result->compiler_args.to_string() == "cl.exe /Yc /c");
  }

  REQUIRE(util::write_file("pch.pch", ""));
  ctx.config.update_from_map({
    {"sloppiness", "pch_defines,time_macros"}
  });

  SUBCASE("Consume PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Yupch.h /Fppch.pch /FIpch.h /Fofoo.cpp.obj /c foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(!ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "foo.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h /c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc and without -Fp and -Fo")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("foo.cpp", ""));
  REQUIRE(util::write_file("pch.h", ""));
  REQUIRE(util::write_file("pch.cpp", ""));

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string("cl.exe /Yc /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "pch.obj");
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Yc");
    CHECK(result->compiler_args.to_string() == "cl.exe /Yc /c");
  }

  REQUIRE(util::write_file("pch.pch", ""));
  ctx.config.update_from_map({
    {"sloppiness", "pch_defines,time_macros"}
  });

  SUBCASE("Consume PCH")
  {
    ctx.orig_args = Args::from_string(
      "cl.exe /Yupch.h /Fppch.pch /FIpch.h /Fofoo.cpp.obj /c foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(!ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "foo.cpp.obj");
    CHECK(result->preprocessor_args.to_string()
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h");
    CHECK(result->compiler_args.to_string()
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h /c");
  }
}

TEST_CASE("MSVC PCH unsupported options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("pch.h", ""));
  REQUIRE(util::write_file("pch.cpp", ""));

  SUBCASE("/Fp with absolute folder path")
  {
    ctx.orig_args =
      Args::from_string("cl.exe /Yc /FpE:\\foo\\bar\\ /c pch.cpp");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::could_not_use_precompiled_header);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.orig_included_pch_file == "E:\\foo\\bar\\");
    CHECK(ctx.args_info.output_obj == "pch.obj");
  }

  SUBCASE("/Fp with relative folder path")
  {
    ctx.orig_args = Args::from_string("cl.exe /Yc /Fpfolder\\ /c pch.cpp");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::could_not_use_precompiled_header);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.orig_included_pch_file == "folder\\");
    CHECK(ctx.args_info.output_obj == "pch.obj");
  }
}

TEST_CASE("MSVC debug information format options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::msvc);
  REQUIRE(util::write_file("foo.c", ""));

  SUBCASE("Only /Z7")
  {
    ctx.orig_args = Args::from_string("cl.exe /c foo.c /Z7");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Z7");
    CHECK(result->compiler_args.to_string() == "cl.exe /Z7 /c");
  }

  SUBCASE("Only /Zi")
  {
    ctx.orig_args = Args::from_string("cl.exe /c foo.c /Zi");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::unsupported_compiler_option);
  }

  SUBCASE("Only /ZI")
  {
    ctx.orig_args = Args::from_string("cl.exe /c foo.c /ZI");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::unsupported_compiler_option);
  }

  SUBCASE("/Z7 + /Zi")
  {
    ctx.orig_args = Args::from_string("cl.exe /Z7 /c foo.c /Zi");
    const auto result = process_args(ctx);
    CHECK(result.error() == Statistic::unsupported_compiler_option);
  }

  SUBCASE("/Zi + /Z7")
  {
    ctx.orig_args = Args::from_string("cl.exe /Zi /c foo.c /Z7");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Zi /Z7");
    CHECK(result->compiler_args.to_string() == "cl.exe /Zi /Z7 /c");
  }
}

// Check that clang-cl debug information is parsed different,
// since for clang-cl /Zi and /Z7 is the same!
TEST_CASE("ClangCL Debug information options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(CompilerType::clang_cl);
  REQUIRE(util::write_file("foo.c", ""));

  SUBCASE("/Z7")
  {
    ctx.orig_args = Args::from_string("clang-cl.exe /c foo.c /Z7");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string() == "clang-cl.exe /Z7");
  }

  SUBCASE("/Zi")
  {
    ctx.orig_args = Args::from_string("clang-cl.exe /c foo.c /Zi");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string() == "clang-cl.exe /Zi");
  }
}

TEST_CASE("Supports -Xarch_host without other -Xarch_*")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args =
    Args::from_string("clang -Xarch_host -foo -c foo.c -Xarch_host -bar");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  REQUIRE(result);
  CHECK(result->preprocessor_args.to_string()
        == "clang -Xarch_host -foo -Xarch_host -bar");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string()
        == "clang -Xarch_host -foo -Xarch_host -bar -c");
}

TEST_CASE("Supports -Xarch_device without other -Xarch_*")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args =
    Args::from_string("clang -Xarch_device -foo -c foo.c -Xarch_device -bar");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  REQUIRE(result);
  CHECK(result->preprocessor_args.to_string()
        == "clang -Xarch_device -foo -Xarch_device -bar");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string()
        == "clang -Xarch_device -foo -Xarch_device -bar -c");
}

TEST_CASE("-Xarch_host with -Xarch_device is too hard")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args =
    Args::from_string("clang -Xarch_device -foo -c foo.c -Xarch_host -bar");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  REQUIRE(!result);
  CHECK(result.error() == Statistic::unsupported_compiler_option);
}

TEST_CASE("-Xarch_host with -Xarch_x86_64 is too hard")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args =
    Args::from_string("clang -Xarch_host -foo -c foo.c -Xarch_x86_64 -bar");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  REQUIRE(!result);
  CHECK(result.error() == Statistic::unsupported_compiler_option);
}

TEST_CASE("-Xarch_device with -Xarch_x86_64 is too hard")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args =
    Args::from_string("clang -Xarch_device -foo -c foo.c -Xarch_x86_64 -bar");
  REQUIRE(util::write_file("foo.c", ""));

  const auto result = process_args(ctx);

  REQUIRE(!result);
  CHECK(result.error() == Statistic::unsupported_compiler_option);
}

TEST_SUITE_END();
