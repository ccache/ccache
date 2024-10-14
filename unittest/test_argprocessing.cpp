// Copyright (C) 2010-2024 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/argprocessing.hpp>
#include <ccache/args.hpp>
#include <ccache/config.hpp>
#include <ccache/context.hpp>
#include <ccache/core/statistic.hpp>
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

namespace {

std::string
get_root()
{
#ifndef _WIN32
  return "/";
#else
  char volume[4]; // "C:\"
  GetVolumePathName(
    util::pstr(*fs::current_path()).c_str(), volume, sizeof(volume));
  return volume;
#endif
}

} // namespace

TEST_SUITE_BEGIN("argprocessing");

TEST_CASE("pass -fsyntax-only to compiler only")
{
  TestContext test_context;
  Context ctx;

  ctx.orig_args = Args::from_string("cc -c foo.c -fsyntax-only");
  util::write_file("foo.c", "");

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

  util::write_file("foo.c", "");
  CHECK(process_args(ctx).error() == Statistic::called_for_preprocessing);
}

TEST_CASE("dash_M_should_be_unsupported")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args = Args::from_string("cc -c foo.c -M");

  util::write_file("foo.c", "");
  CHECK(process_args(ctx).error() == Statistic::unsupported_compiler_option);
}

TEST_CASE("dependency_args_to_preprocessor_if_run_second_cpp_is_false")
{
  TestContext test_context;
  const std::string dep_args =
    "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MP"
    " -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf";
  Context ctx;
  ctx.orig_args = Args::from_string("cc " + dep_args + " -c foo.c -o foo.o");
  util::write_file("foo.c", "");
  ctx.config.set_run_second_cpp(false);

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc " + dep_args);
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -c");
}

TEST_CASE("dependency_args_to_compiler_if_run_second_cpp_is_true")
{
  TestContext test_context;
  const std::string dep_args =
    "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MP"
    " -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf";
  Context ctx;
  ctx.orig_args = Args::from_string("cc " + dep_args + " -c foo.c -o foo.o");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string() == "cc -c " + dep_args);
}

TEST_CASE("cpp_only_args_to_preprocessor_if_run_second_cpp_is_false")
{
  TestContext test_context;
  const std::string cpp_args =
    "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"
    " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."
    " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."
    " -trigraphs -fworking-directory -fno-working-directory";
  const std::string dep_args =
    "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MP"
    " -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf";
  Context ctx;
  ctx.orig_args =
    Args::from_string("cc " + cpp_args + " " + dep_args + " -c foo.c -o foo.o");
  util::write_file("foo.c", "");
  ctx.config.set_run_second_cpp(false);

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string()
        == "cc " + cpp_args + " " + dep_args);
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -c");
}

TEST_CASE(
  "cpp_only_args_to_preprocessor_and_compiler_if_run_second_cpp_is_true")
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
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc " + cpp_args);
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string()
        == "cc " + cpp_args + " -c " + dep_args);
}

TEST_CASE(
  "dependency_args_that_take_an_argument_should_not_require_space_delimiter")
{
  TestContext test_context;
  const std::string dep_args = "-MMD -MFfoo.d -MT mt -MTmt -MQmq";
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c " + dep_args + " foo.c -o foo.o");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == dep_args);
  CHECK(result->compiler_args.to_string() == "cc -c " + dep_args);
}

TEST_CASE("MQ_flag_should_not_be_added_if_run_second_cpp_is_true")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MF foo.d");
  CHECK(result->compiler_args.to_string() == "cc -c -MD -MF foo.d");
}

TEST_CASE("MQ_flag_should_be_added_if_run_second_cpp_is_false")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  util::write_file("foo.c", "");
  ctx.config.set_run_second_cpp(false);

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -MD -MF foo.d -MQ foo.o");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -c");
}

TEST_CASE("MF_should_be_added_if_run_second_cpp_is_false")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MD foo.c -o foo.o");
  util::write_file("foo.c", "");
  ctx.config.set_run_second_cpp(false);

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -MD -MF foo.d -MQ foo.o");
  CHECK(result->extra_args_to_hash.to_string() == "");
  CHECK(result->compiler_args.to_string() == "cc -c");
}

TEST_CASE("MF_should_not_be_added_if_run_second_cpp_is_true")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MD foo.c -o foo.o");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD");
  CHECK(result->compiler_args.to_string() == "cc -c -MD");
}

TEST_CASE("equal_sign_after_MF_should_be_removed")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("cc -c -MF=path foo.c -o foo.o");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MFpath");
  CHECK(result->compiler_args.to_string() == "cc -c -MFpath");
}

TEST_CASE("sysroot_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  util::write_file("foo.c", "");
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

  util::write_file("foo.c", "");
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

  util::write_file("foo.c", "");
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
  ctx.config.update_from_map({{"sloppiness", "ivfsoverlay"}});

  util::write_file("foo.c", "");
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
  ctx.config.update_from_map({{"sloppiness", "modules"}});

  util::write_file("foo.c", "");
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
  ctx.config.update_from_map({{"sloppiness", "modules"}});

  util::write_file("foo.c", "");
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

  util::write_file("foo.c", "");

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string() == "-MMD -MT bar -MFfoo.d");
  CHECK(result->compiler_args.to_string() == "cc -c -MMD -MT bar -MFfoo.d");
}

TEST_CASE("MT_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MT foo -MTbar");

  util::write_file("foo.c", "");

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string()
        == "-MMD -MFfoo.d -MT foo -MTbar");
  CHECK(result->compiler_args.to_string()
        == "cc -c -MMD -MFfoo.d -MT foo -MTbar");
}

TEST_CASE("MQ_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;
  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MQ foo -MQbar");

  util::write_file("foo.c", "");

  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc");
  CHECK(result->extra_args_to_hash.to_string()
        == "-MMD -MFfoo.d -MQ foo -MQbar");
  CHECK(result->compiler_args.to_string()
        == "cc -c -MMD -MFfoo.d -MQ foo -MQbar");
}

TEST_CASE("MQ_flag_without_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d foo.c");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MQ foo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -c -MD -MP -MFfoo.d -MQ foo.d");
}

TEST_CASE("MT_flag_without_immediate_argument_should_not_add_MTobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d foo.c");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MT foo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -c -MD -MP -MFfoo.d -MT foo.d");
}

TEST_CASE("MQ_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d foo.c");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MQfoo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -c -MD -MP -MFfoo.d -MQfoo.d");
}

TEST_CASE("MT_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;
  Context ctx;
  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d foo.c");
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "gcc");
  CHECK(result->extra_args_to_hash.to_string() == "-MD -MP -MFfoo.d -MTfoo.d");
  CHECK(result->compiler_args.to_string()
        == "gcc -c -MD -MP -MFfoo.d -MTfoo.d");
}

TEST_CASE(
  "isystem_flag_with_separate_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  util::write_file("foo.c", "");
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

  util::write_file("foo.c", "");
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

  util::write_file("foo.c", "");
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
  util::write_file("foo.c", "");

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
  util::write_file("foo.c", "");

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
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);

  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cc -g -DX");
  CHECK(result->extra_args_to_hash.to_string()
        == "-Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  CHECK(result->compiler_args.to_string()
        == "cc -g -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error -DX -c");
}

TEST_CASE("cuda_option_file")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::nvcc);
  ctx.orig_args = Args::from_string("nvcc -optf foo.optf,bar.optf");
  util::write_file("foo.c", "");
  util::write_file("foo.optf", "-c foo.c -g -Wall -o");
  util::write_file("bar.optf", "out -DX");

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
  ctx.config.set_compiler_type(Compiler::type::nvcc);
  ctx.orig_args =
    Args::from_string("nvcc -Werror all-warnings -Xcompiler -Werror -c foo.cu");
  util::write_file("foo.cu", "");
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
  ctx.config.set_compiler_type(Compiler::type::nvcc);
  ctx.orig_args = Args::from_string(
    "nvcc --Werror all-warnings -Xcompiler -Werror -c foo.cu");
  util::write_file("foo.cu", "");
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
  ctx.config.set_compiler_type(Compiler::type::clang);

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
  util::write_file("foo.c", "");

  const auto result = process_args(ctx);
  CHECK(result->preprocessor_args.to_string()
        == "clang " + common_args + " " + pch_pth_variants);
  CHECK(result->extra_args_to_hash.to_string() == extra_args);
  CHECK(result->compiler_args.to_string()
        == "clang " + common_args + " " + color_diag + " " + extra_args + " "
             + pch_pth_variants + " -c -fcolor-diagnostics");
}

TEST_CASE("-x")
{
  TestContext test_context;
  Context ctx;
  util::write_file("foo.c", "");

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
  ctx.config.set_compiler_type(Compiler::type::msvc);

  util::write_file("foo.c", "");

  ctx.orig_args = Args::from_string(
    FMT("cl.exe /Fobar.obj /c {}/foo.c /foobar", ctx.actual_cwd));
  const auto result = process_args(ctx);
  CHECK(result);
  CHECK(result->preprocessor_args.to_string() == "cl.exe /foobar");
  CHECK(result->compiler_args.to_string() == "cl.exe /foobar -c");
}

TEST_CASE("MSVC PCH options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.cpp", "");
  util::write_file("pch.h", "");
  util::write_file("pch.cpp", "");

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
          == "cl.exe /Ycpch.h /Fppch.cpp.pch /FIpch.h -c");
  }

  util::write_file("pch.cpp.pch", "");
  ctx.config.update_from_map({{"sloppiness", "pch_defines,time_macros"}});

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
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h -c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.cpp", "");
  util::write_file("pch.h", "");
  util::write_file("pch.cpp", "");

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
          == "cl.exe /Yc /Fppch.cpp.pch /FIpch.h -c");
  }

  util::write_file("pch.cpp.pch", "");
  ctx.config.update_from_map({{"sloppiness", "pch_defines,time_macros"}});

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
          == "cl.exe /Yupch.h /Fppch.cpp.pch /FIpch.h -c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc and without -Fp")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.cpp", "");
  util::write_file("pch.h", "");
  util::write_file("pch.cpp", "");

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string("cl.exe /Yc /Fopch.cpp.obj /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.config.compiler() == Compiler::type::msvc);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "pch.cpp.obj");
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Yc");
    CHECK(result->compiler_args.to_string() == "cl.exe /Yc -c");
  }

  util::write_file("pch.pch", "");
  ctx.config.update_from_map({{"sloppiness", "pch_defines,time_macros"}});

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
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h -c");
  }
}

TEST_CASE("MSVC PCH options with empty -Yc and without -Fp and -Fo")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.cpp", "");
  util::write_file("pch.h", "");
  util::write_file("pch.cpp", "");

  SUBCASE("Create PCH")
  {
    ctx.orig_args = Args::from_string("cl.exe /Yc /c pch.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.generating_pch);
    CHECK(ctx.args_info.included_pch_file == "pch.pch");
    CHECK(ctx.args_info.output_obj == "pch.obj");
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Yc");
    CHECK(result->compiler_args.to_string() == "cl.exe /Yc -c");
  }

  util::write_file("pch.pch", "");
  ctx.config.update_from_map({{"sloppiness", "pch_defines,time_macros"}});

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
          == "cl.exe /Yupch.h /Fppch.pch /FIpch.h -c");
  }
}

TEST_CASE("MSVC PCH unsupported options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("pch.h", "");
  util::write_file("pch.cpp", "");

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
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.c", "");

  SUBCASE("Only /Z7")
  {
    ctx.orig_args = Args::from_string("cl.exe /c foo.c /Z7");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string() == "cl.exe /Z7");
    CHECK(result->compiler_args.to_string() == "cl.exe /Z7 -c");
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
    CHECK(result->compiler_args.to_string() == "cl.exe /Zi /Z7 -c");
  }
}

// Check that clang-cl debug information is parsed different,
// since for clang-cl /Zi and /Z7 is the same!
TEST_CASE("ClangCL Debug information options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::clang_cl);
  util::write_file("foo.c", "");

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

TEST_CASE("MSVC specify object file options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.c", "");

  SUBCASE("cl /c /Fo<file>.obj <file>.c")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Fobar.obj foo.c");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.output_obj == "bar.obj");
  }

  SUBCASE("cl /Fo<file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Fobar.obj foo.c");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.output_obj == "bar.obj");
  }

  SUBCASE("cl /Fo:<file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Fo:bar.obj foo.c");
    const auto result = process_args(ctx);
    CHECK(ctx.args_info.output_obj == "bar.obj");
  }

  SUBCASE("cl /Fo: <file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Fo: bar.obj foo.c");
    const auto result = process_args(ctx);
    CHECK(ctx.args_info.output_obj == "bar.obj");
  }
}

TEST_CASE("MSVC specify source file type options")
{
  TestContext test_context;
  Context ctx;
  ctx.config.set_compiler_type(Compiler::type::msvc);
  util::write_file("foo.c", "");
  util::write_file("foo.cpp", "");

  SUBCASE("cl /TC <file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /TC foo.c");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.c");
  }

  SUBCASE("cl /Tc<file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Tcfoo.c");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.c");
  }

  SUBCASE("cl /Tc <file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Tc foo.c");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.c");
  }

  SUBCASE("cl /TP <file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /TP foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.cpp");
  }

  SUBCASE("cl /Tp<file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Tpfoo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.cpp");
  }

  SUBCASE("cl /Tp <file>")
  {
    ctx.orig_args = Args::from_string("cl.exe /c /Tp foo.cpp");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.input_file == "foo.cpp");
  }
}

#ifdef CCACHE_CXX20_MODULES_FEATURE
TEST_CASE("C++20 modules")
{
  Context ctx;
  ctx.config.set_direct_mode(true);
  ctx.config.set_depend_mode(true);
  ctx.config.set_cxx_modules_mode(true);
  TestContext test_context;
  util::write_file("foo.cpp", "");
  util::write_file("foo.cppm", "");
  util::write_file("foo.ixx", "");

  SUBCASE("cc -c <path>.cpp")
  {
    ctx.config.set_compiler_type(Compiler::type::auto_guess);
    ctx.orig_args = Args::from_string(R"(cc -std=c++20 -c foo.cpp)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.actual_language == "c++");
  }

  SUBCASE("clang++ -x c++-module -c <path>.cpp")
  {
    ctx.config.set_compiler_type(Compiler::type::auto_guess);
    ctx.orig_args =
      Args::from_string(R"(clang++ -std=c++20 -x c++-module -c foo.cpp)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("cl /c /interface /Tp <path>.cpp")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args =
      Args::from_string(R"(cl.exe /std:c++20 /c /interface /Tp foo.cpp)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("cc -c <path>.cppm")
  {
    ctx.config.set_compiler_type(Compiler::type::auto_guess);
    ctx.orig_args = Args::from_string(R"(cc -std=c++20 -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("cc -c <path>.ixx")
  {
    ctx.config.set_compiler_type(Compiler::type::auto_guess);
    ctx.orig_args = Args::from_string(R"(cc -std=c++20 -c foo.ixx)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("g++ -fmodules")
  {
    ctx.config.set_compiler_type(Compiler::type::gcc);
    ctx.orig_args =
      Args::from_string(R"(g++ -std=gnu++20 -fmodules -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string()
          == R"(g++ -std=gnu++20 -fmodules)");
    CHECK(result->compiler_args.to_string()
          == R"(g++ -std=gnu++20 -fmodules -c -fdiagnostics-color)");
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("g++ -fmodules-ts")
  {
    ctx.config.set_compiler_type(Compiler::type::gcc);
    ctx.orig_args =
      Args::from_string(R"(g++ -std=gnu++20 -fmodules-ts -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string()
          == R"(g++ -std=gnu++20 -fmodules-ts)");
    CHECK(result->compiler_args.to_string()
          == R"(g++ -std=gnu++20 -fmodules-ts -c -fdiagnostics-color)");
    CHECK(ctx.args_info.actual_language == "c++-module");
  }

  SUBCASE("g++ -fdeps-format=p1689r5")
  {
    ctx.config.set_compiler_type(Compiler::type::gcc);
    ctx.orig_args = Args::from_string(
      R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5 -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(result->preprocessor_args.to_string()
          == R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5)");
    CHECK(
      result->compiler_args.to_string()
      == R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5 -c -fdiagnostics-color)");
    CHECK(ctx.args_info.cxx_modules.ddi_format == "p1689r5");
  }

  SUBCASE("g++ -fdeps-file=")
  {
    ctx.config.set_compiler_type(Compiler::type::gcc);
    ctx.orig_args = Args::from_string(
      R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5 -fdeps-file=foo.ddi -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(
      result->preprocessor_args.to_string()
      == R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5 -fdeps-file=foo.ddi)");
    CHECK(
      result->compiler_args.to_string()
      == R"(g++ -std=gnu++20 -fmodules -fdeps-format=p1689r5 -fdeps-file=foo.ddi -c -fdiagnostics-color)");
    CHECK(ctx.args_info.cxx_modules.output_ddi == "foo.ddi");
  }

  SUBCASE("cl /scanDependencies-")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /scanDependencies- /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result.error() == Statistic::called_for_preprocessing);
    CHECK(!ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.output_ddi == "-");
  }

  SUBCASE("cl /scanDependencies<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /scanDependenciesfoo.ddi /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result.error() == Statistic::called_for_preprocessing);
    CHECK(!ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.output_ddi == "foo.ddi");
  }

  SUBCASE("cl /scanDependencies -")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /scanDependencies - /c /interface /Tp foo.cpp)");
    const auto result = process_args(ctx);
    REQUIRE(result.error() == Statistic::called_for_preprocessing);
    CHECK(!ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.output_ddi == "-");
  }

  SUBCASE("cl /scanDependencies <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /scanDependencies foo.ddi /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result.error() == Statistic::called_for_preprocessing);
    CHECK(!ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.output_ddi == "foo.ddi");
  }

  SUBCASE("cl /sourceDependencies-")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /sourceDependencies- /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.generating_msvc_source_dependencies);
    CHECK(ctx.args_info.cxx_modules.output_msvc_source_dependencies == "-");
  }

  SUBCASE("cl /sourceDependencies<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /sourceDependenciesfoo.json /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.generating_msvc_source_dependencies);
    CHECK(ctx.args_info.cxx_modules.output_msvc_source_dependencies
          == "foo.json");
  }

  SUBCASE("cl /sourceDependencies -")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /sourceDependencies - /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.generating_msvc_source_dependencies);
    CHECK(ctx.args_info.cxx_modules.output_msvc_source_dependencies == "-");
  }

  SUBCASE("cl /sourceDependencies <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 /sourceDependencies foo.json /c /interface /Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.expect_output_obj);
    CHECK(ctx.args_info.cxx_modules.generating_msvc_source_dependencies);
    CHECK(ctx.args_info.cxx_modules.output_msvc_source_dependencies
          == "foo.json");
  }

  SUBCASE("clang++ --precompile")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-output --precompile -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.generating_bmi);
    CHECK(ctx.args_info.cxx_modules.precompiling_bmi);
    CHECK(!ctx.args_info.expect_output_obj);
  }

  SUBCASE("cl /ifcOnly")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcOnly -c -interface -Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.generating_bmi);
    CHECK(ctx.args_info.cxx_modules.precompiling_bmi);
    CHECK(!ctx.args_info.expect_output_obj);
  }

  SUBCASE("clang++ -fmodule-output")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args =
      Args::from_string(R"(clang++ -std=gnu++20 -fmodule-output -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.generating_bmi);
  }

  SUBCASE("clang++ -fmodule-output=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-output=foo.pcm -c foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.generating_bmi);
    CHECK(ctx.args_info.cxx_modules.output_bmi == "foo.pcm");
  }

  SUBCASE("cl /ifcOutput<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcOutputfoo -c -interface -Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.output_bmi == "foo");
  }

  SUBCASE("cl /ifcOutput <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcOutput foo.ifc -c -interface -Tp foo.cppm)");
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.output_bmi == "foo.ifc");
  }

  SUBCASE("clang++ -fmodule-file=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-file=bar.pcm -c foo.cppm)");
    const std::vector<std::string> expected = {"bar.pcm"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("clang++ -fmodule-file=<path> ... -fmodule-file=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-file=bar.pcm -fmodule-file=baz.pcm -fmodule-file=qux.pcm -c foo.cppm)");
    const std::vector<std::string> expected = {"bar.pcm", "baz.pcm", "qux.pcm"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("clang++ -fmodule-file=<path>=<name>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-file=bar=bar.pcm -c foo.cppm)");
    const std::unordered_map<std::string, std::string> expected = {
      {"bar", "bar.pcm"}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("clang++ -fmodule-file=<path>=<name> ... -fmodule-file=<path>=<name>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fmodule-file=bar=bar.pcm -fmodule-file=baz=baz.pcm -fmodule-file=qux=qux.pcm -c foo.cppm)");
    const std::unordered_map<std::string, std::string> expected = {
      {{"bar", "bar.pcm"}, {"baz", "baz.pcm"}, {"qux", "qux.pcm"}}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("cl /reference<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      "cl.exe /std:c++20 -referencebar.ifc -c -interface -Tp foo.cppm");
    const std::vector<std::string> expected = {"bar.ifc"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("cl /reference <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -reference bar.ifc -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar.ifc"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("cl /reference<path> ... /reference<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -referencebar.ifc -referencebaz.ifc -referencequx.ifc -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar.ifc", "baz.ifc", "qux.ifc"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("cl /reference <path> ... /reference <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -reference bar.ifc -reference baz.ifc -reference qux.ifc -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar.ifc", "baz.ifc", "qux.ifc"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.units_paths == expected);
  }

  SUBCASE("cl /reference<name>=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      "cl.exe /std:c++20 -referencebar=bar.ifc -c -interface -Tp foo.cppm");
    const std::unordered_map<std::string, std::string> expected = {
      {{"bar", "bar.ifc"}}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("cl /reference <name>=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -reference bar=bar.ifc -c -interface -Tp foo.cppm)");
    const std::unordered_map<std::string, std::string> expected = {
      {{"bar", "bar.ifc"}}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("cl /reference<name>=<path> ... /reference<name>=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -referencebar=bar.ifc -referencebaz=baz.ifc -referencequx=qux.ifc -c -interface -Tp foo.cppm)");
    const std::unordered_map<std::string, std::string> expected = {
      {{"bar", "bar.ifc"}, {"baz", "baz.ifc"}, {"qux", "qux.ifc"}}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("cl /reference <name>=<path> ... /reference <name>=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -reference bar=bar.ifc -reference baz=baz.ifc -reference qux=qux.ifc -c -interface -Tp foo.cppm)");
    const std::unordered_map<std::string, std::string> expected = {
      {{"bar", "bar.ifc"}, {"baz", "baz.ifc"}, {"qux", "qux.ifc"}}};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.names_paths == expected);
  }

  SUBCASE("clang++ -fprebuilt-module-path=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fprebuilt-module-path=bar -c foo.cppm)");
    const std::vector<std::string> expected = {"bar"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }

  SUBCASE(
    "clang++ -fprebuilt-module-path=<path> ... -fprebuilt-module-path=<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::clang);
    ctx.orig_args = Args::from_string(
      R"(clang++ -std=gnu++20 -fprebuilt-module-path=bar -fprebuilt-module-path=baz -fprebuilt-module-path=qux -c foo.cppm)");
    const std::vector<std::string> expected = {"bar", "baz", "qux"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }

  SUBCASE("cl /ifcSearchDir<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      "cl.exe /std:c++20 -ifcSearchDirbar -c -interface -Tp foo.cppm");
    const std::vector<std::string> expected = {"bar"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }

  SUBCASE("cl /ifcSearchDir<path> ... /ifcSearchDir<path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcSearchDirbar -ifcSearchDirbaz -ifcSearchDirqux -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar", "baz", "qux"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }

  SUBCASE("cl /ifcSearchDir <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcSearchDir bar -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }

  SUBCASE("cl /ifcSearchDir <path> ... /ifcSearchDir <path>")
  {
    ctx.config.set_compiler_type(Compiler::type::msvc);
    ctx.orig_args = Args::from_string(
      R"(cl.exe /std:c++20 -ifcSearchDir bar -ifcSearchDir baz -ifcSearchDir qux -c -interface -Tp foo.cppm)");
    const std::vector<std::string> expected = {"bar", "baz", "qux"};
    const auto result = process_args(ctx);
    REQUIRE(result);
    CHECK(ctx.args_info.cxx_modules.search_dirs == expected);
  }
}
#endif // CCACHE_CXX20_MODULES_FEATURE

TEST_SUITE_END();
