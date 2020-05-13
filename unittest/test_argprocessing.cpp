// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "../src/Args.hpp"
#include "../src/Config.hpp"
#include "../src/Context.hpp"
#include "../src/Util.hpp"
#include "../src/ccache.hpp"
#include "../src/stats.hpp"
#include "TestUtil.hpp"
#include "argprocessing.hpp"

#include "third_party/catch.hpp"

using TestUtil::TestContext;

namespace {

std::string
get_root()
{
#ifndef _WIN32
  return "/";
#else
  char volume[4]; // "C:\"
  GetVolumePathName(Util::get_actual_cwd().c_str(), volume, sizeof(volume));
  volume[2] = '/'; // Since base directory is normalized to forward slashes
  return volume;
#endif
}

std::string
get_posix_path(const std::string& path)
{
#ifndef _WIN32
  return path;
#else
  std::string posix;

  // /-escape volume.
  if (path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':') {
    posix = "/" + path;
  } else {
    posix = path;
  }
  // Convert slashes.
  std::replace(posix.begin(), posix.end(), '\\', '/');
  return posix;
#endif
}

} // namespace

TEST_CASE("dash_E_should_result_in_called_for_preprocessing")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c foo.c -E");
  Args preprocessed;
  Args extra;
  Args compiler;

  Util::write_file("foo.c", "");
  CHECK(process_args(ctx, preprocessed, extra, compiler)
        == STATS_PREPROCESSING);
}

TEST_CASE("dash_M_should_be_unsupported")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c foo.c -M");
  Args preprocessed;
  Args extra;
  Args compiler;

  Util::write_file("foo.c", "");
  CHECK(process_args(ctx, preprocessed, extra, compiler)
        == STATS_UNSUPPORTED_OPTION);
}

TEST_CASE("dependency_args_to_preprocessor_if_run_second_cpp_is_false")
{
  TestContext test_context;

  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  ctx.orig_args = Args::from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc " DEP_ARGS);
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -c");
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("dependency_args_to_compiler_if_run_second_cpp_is_true")
{
  TestContext test_context;

  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  ctx.orig_args = Args::from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string(DEP_ARGS);
  Args exp_cc = Args::from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("cpp_only_args_to_preprocessor_if_run_second_cpp_is_false")
{
  TestContext test_context;

  Context ctx;

#define CPP_ARGS                                                               \
  "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"      \
  " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."         \
  " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."      \
  " -trigraphs -fworking-directory -fno-working-directory"
#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  ctx.orig_args =
    Args::from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc " CPP_ARGS " " DEP_ARGS);
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -c");
#undef DEP_ARGS
#undef CPP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE(
  "cpp_only_args_to_preprocessor_and_compiler_if_run_second_cpp_is_true")
{
  TestContext test_context;

  Context ctx;

#define CPP_ARGS                                                               \
  "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"      \
  " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."         \
  " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."      \
  " -trigraphs -fworking-directory -fno-working-directory"
#define DEP_ARGS                                                               \
  " -MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"       \
  " -Wp,-MMD,wpmmd"
  ctx.orig_args =
    Args::from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc " CPP_ARGS);
  Args exp_extra = Args::from_string(DEP_ARGS);
  Args exp_cc = Args::from_string("cc " CPP_ARGS " -c " DEP_ARGS);
#undef DEP_ARGS
#undef CPP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE(
  "dependency_args_that_take_an_argument_should_not_require_space_delimiter")
{
  TestContext test_context;

  Context ctx;

#define DEP_ARGS "-MMD -MFfoo.d -MT mt -MTmt -MQmq"
  ctx.orig_args = Args::from_string("cc -c " DEP_ARGS " foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string(DEP_ARGS);
  Args exp_cc = Args::from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MQ_flag_should_not_be_added_if_run_second_cpp_is_true")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MD -MF foo.d");
  Args exp_cc = Args::from_string("cc -c -MD -MF foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MQ_flag_should_be_added_if_run_second_cpp_is_false")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  Args exp_cpp = Args::from_string("cc -MD -MF foo.d -MQ foo.o");
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  Util::write_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MF_should_be_added_if_run_second_cpp_is_false")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c -MD foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc -MD -MF foo.d -MQ foo.o");
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MF_should_not_be_added_if_run_second_cpp_is_true")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c -MD foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MD");
  Args exp_cc = Args::from_string("cc -c -MD");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("equal_sign_after_MF_should_be_removed")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -c -MF=path foo.c -o foo.o");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MFpath");
  Args exp_cc = Args::from_string("cc -c -MFpath");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("sysroot_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    fmt::format("cc --sysroot={}/foo/bar -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(act_cpp[1] == "--sysroot=./foo/bar");
}

TEST_CASE(
  "sysroot_with_separate_argument_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    fmt::format("cc --sysroot {}/foo -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(act_cpp[1] == "--sysroot");
  CHECK(act_cpp[2] == "./foo");
}

TEST_CASE("MF_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MT bar -MFfoo.d");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MMD -MT bar -MFfoo.d");
  Args exp_cc = Args::from_string("cc -c -MMD -MT bar -MFfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MT_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MT foo -MTbar");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MMD -MFfoo.d -MT foo -MTbar");
  Args exp_cc = Args::from_string("cc -c -MMD -MFfoo.d -MT foo -MTbar");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MQ_flag_with_immediate_argument_should_work_as_last_argument")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args =
    Args::from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MQ foo -MQbar");
  Args exp_cpp = Args::from_string("cc");
  Args exp_extra = Args::from_string("-MMD -MFfoo.d -MQ foo -MQbar");
  Args exp_cc = Args::from_string("cc -c -MMD -MFfoo.d -MQ foo -MQbar");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MQ_flag_without_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d foo.c");
  Args exp_cpp = Args::from_string("gcc");
  Args exp_extra = Args::from_string("-MD -MP -MFfoo.d -MQ foo.d");
  Args exp_cc = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MT_flag_without_immediate_argument_should_not_add_MTobj")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d foo.c");
  Args exp_cpp = Args::from_string("gcc");
  Args exp_extra = Args::from_string("-MD -MP -MFfoo.d -MT foo.d");
  Args exp_cc = Args::from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MQ_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d foo.c");
  Args exp_cpp = Args::from_string("gcc");
  Args exp_extra = Args::from_string("-MD -MP -MFfoo.d -MQfoo.d");
  Args exp_cc = Args::from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("MT_flag_with_immediate_argument_should_not_add_MQobj")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d foo.c");
  Args exp_cpp = Args::from_string("gcc");
  Args exp_extra = Args::from_string("-MD -MP -MFfoo.d -MTfoo.d");
  Args exp_cc = Args::from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE(
  "isystem_flag_with_separate_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  std::string arg_string =
    fmt::format("cc -isystem {}/foo -c foo.c", ctx.actual_cwd);
  ctx.orig_args = Args::from_string(arg_string);

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK("./foo" == act_cpp[2]);
}

TEST_CASE("isystem_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  ctx.config.set_base_dir("/"); // posix
  // Windows path doesn't work concatenated.
  std::string cwd = get_posix_path(ctx.actual_cwd);
  std::string arg_string = fmt::format("cc -isystem{}/foo -c foo.c", cwd);
  ctx.orig_args = Args::from_string(arg_string);

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK("-isystem./foo" == act_cpp[1]);
}

TEST_CASE("I_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used")
{
  TestContext test_context;

  Context ctx;

  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  ctx.config.set_base_dir(x_strdup("/")); // posix
  // Windows path doesn't work concatenated.
  std::string cwd = get_posix_path(ctx.actual_cwd);
  std::string arg_string = fmt::format("cc -I{}/foo -c foo.c", cwd);
  ctx.orig_args = Args::from_string(arg_string);

  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK("-I./foo" == act_cpp[1]);
}

TEST_CASE("debug_flag_order_with_known_option_first")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -g1 -gsplit-dwarf foo.c -c");
  Args exp_cpp = Args::from_string("cc -g1 -gsplit-dwarf");
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -g1 -gsplit-dwarf -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("debug_flag_order_with_known_option_last")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string("cc -gsplit-dwarf -g1 foo.c -c");
  Args exp_cpp = Args::from_string("cc -gsplit-dwarf -g1");
  Args exp_extra;
  Args exp_cc = Args::from_string("cc -gsplit-dwarf -g1 -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("options_not_to_be_passed_to_the_preprocesor")
{
  TestContext test_context;

  Context ctx;

  ctx.orig_args = Args::from_string(
    "cc -Wa,foo foo.c -g -c -DX -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  Args exp_cpp = Args::from_string("cc -g -DX");
  Args exp_extra =
    Args::from_string(" -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  Args exp_cc = Args::from_string(
    "cc -g -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error -DX -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}

TEST_CASE("cuda_option_file")
{
  TestContext test_context;

  Context ctx;
  ctx.guessed_compiler = GuessedCompiler::nvcc;

  ctx.orig_args = Args::from_string("nvcc -optf foo.optf,bar.optf");
  Args exp_cpp = Args::from_string("nvcc -g -Wall -DX");
  Args exp_extra = Args::from_string("");
  Args exp_cc = Args::from_string("nvcc -g -Wall -DX -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  Util::write_file("foo.c", "");
  Util::write_file("foo.optf", "-c foo.c -g -Wall -o");
  Util::write_file("bar.optf", "out -DX");
  CHECK(!process_args(ctx, act_cpp, act_extra, act_cc));
  CHECK(exp_cpp == act_cpp);
  CHECK(exp_extra == act_extra);
  CHECK(exp_cc == act_cc);
}
