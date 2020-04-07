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

// This file contains tests for the processing of compiler arguments.

#include "../src/Args.hpp"
#include "../src/Config.hpp"
#include "../src/Context.hpp"
#include "../src/Util.hpp"
#include "../src/ccache.hpp"
#include "../src/stats.hpp"
#include "framework.hpp"
#include "util.hpp"

static std::string
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

static char*
get_posix_path(const char* path)
{
#ifndef _WIN32
  return x_strdup(path);
#else
  char* posix;
  char* p;

  // /-escape volume.
  if (path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':') {
    posix = format("/%s", path);
  } else {
    posix = x_strdup(path);
  }
  // Convert slashes.
  for (p = posix; *p; p++) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  return posix;
#endif
}

TEST_SUITE(argument_processing)

TEST(dash_E_should_result_in_called_for_preprocessing)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c foo.c -E");
  Args preprocessed;
  Args extra;
  Args compiler;

  create_file("foo.c", "");
  CHECK(process_args(ctx, orig, preprocessed, extra, compiler)
        == STATS_PREPROCESSING);
}

TEST(dash_M_should_be_unsupported)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c foo.c -M");
  Args preprocessed;
  Args extra;
  Args compiler;

  create_file("foo.c", "");
  CHECK(process_args(ctx, orig, preprocessed, extra, compiler)
        == STATS_UNSUPPORTED_OPTION);
}

TEST(dependency_args_to_preprocessor_if_run_second_cpp_is_false)
{
  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  Args orig = args_init_from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc " DEP_ARGS);
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -c");
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(dependency_args_to_compiler_if_run_second_cpp_is_true)
{
  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  Args orig = args_init_from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string(DEP_ARGS);
  Args exp_cc = args_init_from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(cpp_only_args_to_preprocessor_if_run_second_cpp_is_false)
{
  Context ctx;

#define CPP_ARGS                                                               \
  "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"      \
  " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."         \
  " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."      \
  " -trigraphs -fworking-directory -fno-working-directory"
#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  Args orig =
    args_init_from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc " CPP_ARGS " " DEP_ARGS);
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -c");
#undef DEP_ARGS
#undef CPP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(cpp_only_args_to_preprocessor_and_compiler_if_run_second_cpp_is_true)
{
  Context ctx;

#define CPP_ARGS                                                               \
  "-I. -idirafter . -iframework. -imacros . -imultilib . -include test.h"      \
  " -include-pch test.pch -iprefix . -iquote . -isysroot . -isystem ."         \
  " -iwithprefix . -iwithprefixbefore . -DTEST_MACRO -DTEST_MACRO2=1 -F."      \
  " -trigraphs -fworking-directory -fno-working-directory"
#define DEP_ARGS                                                               \
  " -MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"       \
  " -Wp,-MMD,wpmmd"
  Args orig =
    args_init_from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc " CPP_ARGS);
  Args exp_extra = args_init_from_string(DEP_ARGS);
  Args exp_cc = args_init_from_string("cc " CPP_ARGS " -c " DEP_ARGS);
#undef DEP_ARGS
#undef CPP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(dependency_args_that_take_an_argument_should_not_require_space_delimiter)
{
  Context ctx;

#define DEP_ARGS "-MMD -MFfoo.d -MT mt -MTmt -MQmq"
  Args orig = args_init_from_string("cc -c " DEP_ARGS " foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string(DEP_ARGS);
  Args exp_cc = args_init_from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MQ_flag_should_not_be_added_if_run_second_cpp_is_true)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MD -MF foo.d");
  Args exp_cc = args_init_from_string("cc -c -MD -MF foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MQ_flag_should_be_added_if_run_second_cpp_is_false)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  Args exp_cpp = args_init_from_string("cc -MD -MF foo.d -MQ foo.o");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;
  create_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MF_should_be_added_if_run_second_cpp_is_false)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c -MD foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc -MD -MF foo.d -MQ foo.o");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  ctx.config.set_run_second_cpp(false);
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MF_should_not_be_added_if_run_second_cpp_is_true)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c -MD foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MD");
  Args exp_cc = args_init_from_string("cc -c -MD");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(equal_sign_after_MF_should_be_removed)
{
  Context ctx;

  Args orig = args_init_from_string("cc -c -MF=path foo.c -o foo.o");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MFpath");
  Args exp_cc = args_init_from_string("cc -c -MFpath");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(sysroot_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  char* arg_string;
  Args orig;
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  arg_string =
    format("cc --sysroot=%s/foo/bar -c foo.c", ctx.actual_cwd.c_str());
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_STR_EQ(act_cpp->argv[1], "--sysroot=./foo/bar");
}

TEST(sysroot_with_separate_argument_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  char* arg_string;
  Args orig;
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  arg_string = format("cc --sysroot %s/foo -c foo.c", ctx.actual_cwd.c_str());
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_STR_EQ(act_cpp->argv[1], "--sysroot");
  CHECK_STR_EQ(act_cpp->argv[2], "./foo");
}

TEST(MF_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  Args orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MT bar -MFfoo.d");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MMD -MT bar -MFfoo.d");
  Args exp_cc = args_init_from_string("cc -c -MMD -MT bar -MFfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MT_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  Args orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MT foo -MTbar");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MMD -MFfoo.d -MT foo -MTbar");
  Args exp_cc = args_init_from_string("cc -c -MMD -MFfoo.d -MT foo -MTbar");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MQ_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  Args orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MQ foo -MQbar");
  Args exp_cpp = args_init_from_string("cc");
  Args exp_extra = args_init_from_string("-MMD -MFfoo.d -MQ foo -MQbar");
  Args exp_cc = args_init_from_string("cc -c -MMD -MFfoo.d -MQ foo -MQbar");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MQ_flag_without_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d foo.c");
  Args exp_cpp = args_init_from_string("gcc");
  Args exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MQ foo.d");
  Args exp_cc = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MT_flag_without_immediate_argument_should_not_add_MTobj)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d foo.c");
  Args exp_cpp = args_init_from_string("gcc");
  Args exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MT foo.d");
  Args exp_cc = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MQ_flag_with_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d foo.c");
  Args exp_cpp = args_init_from_string("gcc");
  Args exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MQfoo.d");
  Args exp_cc = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(MT_flag_with_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d foo.c");
  Args exp_cpp = args_init_from_string("gcc");
  Args exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MTfoo.d");
  Args exp_cc = args_init_from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(fprofile_flag_with_existing_dir_should_be_rewritten_to_real_path)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -fprofile-generate=some/dir foo.c");
  Args exp_cpp = args_init_from_string("gcc");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("gcc");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  char* s;

  create_file("foo.c", "");
  mkdir("some", 0777);
  mkdir("some/dir", 0777);
  std::string path = Util::real_path("some/dir");
  s = format("-fprofile-generate=%s", path.c_str());
  args_add(exp_cpp, s);
  args_add(exp_cc, s);
  args_add(exp_cc, "-c");
  free(s);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(fprofile_flag_with_nonexistent_dir_should_not_be_rewritten)
{
  Context ctx;

  Args orig = args_init_from_string("gcc -c -fprofile-generate=some/dir foo.c");
  Args exp_cpp = args_init_from_string("gcc -fprofile-generate=some/dir");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("gcc -fprofile-generate=some/dir -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(isystem_flag_with_separate_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  char* arg_string;
  Args orig;
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  ctx.config.set_base_dir(get_root());
  arg_string = format("cc -isystem %s/foo -c foo.c", ctx.actual_cwd.c_str());
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_STR_EQ("./foo", act_cpp->argv[2]);
}

TEST(isystem_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  char* cwd;
  char* arg_string;
  Args orig;
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  ctx.config.set_base_dir("/"); // posix
  // Windows path doesn't work concatenated.
  cwd = get_posix_path(ctx.actual_cwd.c_str());
  arg_string = format("cc -isystem%s/foo -c foo.c", cwd);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_STR_EQ("-isystem./foo", act_cpp->argv[1]);

  free(cwd);
}

TEST(I_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  char* cwd;
  char* arg_string;
  Args orig;
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  ctx.config.set_base_dir(x_strdup("/")); // posix
  // Windows path doesn't work concatenated.
  cwd = get_posix_path(ctx.actual_cwd.c_str());
  arg_string = format("cc -I%s/foo -c foo.c", cwd);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_STR_EQ("-I./foo", act_cpp->argv[1]);

  free(cwd);
}

TEST(debug_flag_order_with_known_option_first)
{
  Context ctx;

  Args orig = args_init_from_string("cc -g1 -gsplit-dwarf foo.c -c");
  Args exp_cpp = args_init_from_string("cc -g1 -gsplit-dwarf");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -g1 -gsplit-dwarf -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(debug_flag_order_with_known_option_last)
{
  Context ctx;

  Args orig = args_init_from_string("cc -gsplit-dwarf -g1 foo.c -c");
  Args exp_cpp = args_init_from_string("cc -gsplit-dwarf -g1");
  Args exp_extra = args_init(0, NULL);
  Args exp_cc = args_init_from_string("cc -gsplit-dwarf -g1 -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST(options_not_to_be_passed_to_the_preprocesor)
{
  Context ctx;

  Args orig = args_init_from_string(
    "cc -Wa,foo foo.c -g -c -DX -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  Args exp_cpp = args_init_from_string("cc -g -DX");
  Args exp_extra = args_init_from_string(
    " -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  Args exp_cc = args_init_from_string(
    "cc -g -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error -DX -c");
  Args act_cpp;
  Args act_extra;
  Args act_cc;

  create_file("foo.c", "");
  CHECK(!process_args(ctx, orig, act_cpp, act_extra, act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);
}

TEST_SUITE_END
