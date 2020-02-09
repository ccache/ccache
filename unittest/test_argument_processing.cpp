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

#include "../src/Config.hpp"
#include "../src/Context.hpp"
#include "../src/args.hpp"
#include "../src/ccache.hpp"
#include "../src/legacy_globals.hpp"
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
  GetVolumePathName(get_cwd(), volume, sizeof(volume));
  return volume;
#endif
}

static char*
get_posix_path(char* path)
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

bool cc_process_args(Context& ctx,
                     struct args* args,
                     struct args** preprocessor_args,
                     struct args** extra_args_to_hash,
                     struct args** compiler_args);

bool
cc_process_args(Context& ctx,
                struct args* args,
                struct args** preprocessor_args,
                struct args** extra_args_to_hash,
                struct args** compiler_args)
{
  bool success = cc_process_args(ctx.args_info,
                                 g_config,
                                 args,
                                 preprocessor_args,
                                 extra_args_to_hash,
                                 compiler_args);

  output_dep = x_strdup(ctx.args_info.output_dep.c_str());
  output_cov = x_strdup(ctx.args_info.output_cov.c_str());
  output_su = x_strdup(ctx.args_info.output_su.c_str());
  output_dia = x_strdup(ctx.args_info.output_dia.c_str());
  output_dwo = x_strdup(ctx.args_info.output_dwo.c_str());

  actual_language = x_strdup(ctx.args_info.actual_language.c_str());

  generating_dependencies = ctx.args_info.generating_dependencies;
  generating_coverage = ctx.args_info.generating_coverage;
  generating_stackusage = ctx.args_info.generating_stackusage;
  generating_diagnostics = ctx.args_info.generating_diagnostics;
  seen_split_dwarf = ctx.args_info.seen_split_dwarf;
  profile_arcs = ctx.args_info.profile_arcs;
  profile_dir = x_strdup(ctx.args_info.profile_dir.c_str());

  direct_i_file = ctx.args_info.direct_i_file;
  output_is_precompiled_header = ctx.args_info.output_is_precompiled_header;
  profile_use = ctx.args_info.profile_use;
  profile_generate = ctx.args_info.profile_generate;
  using_precompiled_header = ctx.args_info.using_precompiled_header;

  return success;
}

TEST_SUITE(argument_processing)

TEST(dash_E_should_result_in_called_for_preprocessing)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -c foo.c -E");
  struct args *preprocessed, *compiler;

  create_file("foo.c", "");
  CHECK(!cc_process_args(ctx, orig, &preprocessed, NULL, &compiler));
  CHECK_INT_EQ(1, stats_get_pending(STATS_PREPROCESSING));

  args_free(orig);
}

TEST(dash_M_should_be_unsupported)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -c foo.c -M");
  struct args *preprocessed, *compiler;

  create_file("foo.c", "");
  CHECK(!cc_process_args(ctx, orig, &preprocessed, NULL, &compiler));
  CHECK_INT_EQ(1, stats_get_pending(STATS_UNSUPPORTED_OPTION));

  args_free(orig);
}

TEST(dependency_args_to_preprocessor_if_run_second_cpp_is_false)
{
  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  struct args* orig =
    args_init_from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc " DEP_ARGS);
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -c");
#undef DEP_ARGS
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  g_config.set_run_second_cpp(false);
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(dependency_args_to_compiler_if_run_second_cpp_is_true)
{
  Context ctx;

#define DEP_ARGS                                                               \
  "-MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2 -Wp,-MD,wpmd"        \
  " -Wp,-MMD,wpmmd -Wp,-MP -Wp,-MT,wpmt -Wp,-MQ,wpmq -Wp,-MF,wpf"
  struct args* orig =
    args_init_from_string("cc " DEP_ARGS " -c foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string(DEP_ARGS);
  struct args* exp_cc = args_init_from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
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
  struct args* orig =
    args_init_from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc " CPP_ARGS " " DEP_ARGS);
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -c");
#undef DEP_ARGS
#undef CPP_ARGS
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  g_config.set_run_second_cpp(false);
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
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
  struct args* orig =
    args_init_from_string("cc " CPP_ARGS " " DEP_ARGS " -c foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc " CPP_ARGS);
  struct args* exp_extra = args_init_from_string(DEP_ARGS);
  struct args* exp_cc = args_init_from_string("cc " CPP_ARGS " -c " DEP_ARGS);
#undef DEP_ARGS
#undef CPP_ARGS
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(dependency_args_that_take_an_argument_should_not_require_space_delimiter)
{
  Context ctx;

#define DEP_ARGS "-MMD -MFfoo.d -MT mt -MTmt -MQmq"
  struct args* orig =
    args_init_from_string("cc -c " DEP_ARGS " foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string(DEP_ARGS);
  struct args* exp_cc = args_init_from_string("cc -c " DEP_ARGS);
#undef DEP_ARGS
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MQ_flag_should_not_be_added_if_run_second_cpp_is_true)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string("-MD -MF foo.d");
  struct args* exp_cc = args_init_from_string("cc -c -MD -MF foo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MQ_flag_should_be_added_if_run_second_cpp_is_false)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("cc -c -MD foo.c -MF foo.d -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc -MD -MF foo.d -MQ foo.o");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;
  create_file("foo.c", "");

  g_config.set_run_second_cpp(false);
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MF_should_be_added_if_run_second_cpp_is_false)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -c -MD foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc -MD -MF foo.d -MQ foo.o");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  g_config.set_run_second_cpp(false);
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MF_should_not_be_added_if_run_second_cpp_is_true)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -c -MD foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string("-MD");
  struct args* exp_cc = args_init_from_string("cc -c -MD");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(equal_sign_after_MF_should_be_removed)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -c -MF=path foo.c -o foo.o");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string("-MFpath");
  struct args* exp_cc = args_init_from_string("cc -c -MFpath");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(sysroot_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  extern char* current_working_dir;
  char* arg_string;
  struct args* orig;
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  g_config.set_base_dir(get_root());
  current_working_dir = get_cwd();
  arg_string = format("cc --sysroot=%s/foo/bar -c foo.c", current_working_dir);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_STR_EQ(act_cpp->argv[1], "--sysroot=./foo/bar");

  args_free(orig);
  args_free(act_cpp);
  args_free(act_cc);
}

TEST(sysroot_with_separate_argument_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  extern char* current_working_dir;
  char* arg_string;
  struct args* orig;
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  g_config.set_base_dir(get_root());
  current_working_dir = get_cwd();
  arg_string = format("cc --sysroot %s/foo -c foo.c", current_working_dir);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_STR_EQ(act_cpp->argv[1], "--sysroot");
  CHECK_STR_EQ(act_cpp->argv[2], "./foo");

  args_free(orig);
  args_free(act_cpp);
  args_free(act_cc);
}

TEST(MF_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MT bar -MFfoo.d");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra = args_init_from_string("-MMD -MT bar -MFfoo.d");
  struct args* exp_cc = args_init_from_string("cc -c -MMD -MT bar -MFfoo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MT_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MT foo -MTbar");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra =
    args_init_from_string("-MMD -MFfoo.d -MT foo -MTbar");
  struct args* exp_cc =
    args_init_from_string("cc -c -MMD -MFfoo.d -MT foo -MTbar");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MQ_flag_with_immediate_argument_should_work_as_last_argument)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("cc -c foo.c -o foo.o -MMD -MFfoo.d -MQ foo -MQbar");
  struct args* exp_cpp = args_init_from_string("cc");
  struct args* exp_extra =
    args_init_from_string("-MMD -MFfoo.d -MQ foo -MQbar");
  struct args* exp_cc =
    args_init_from_string("cc -c -MMD -MFfoo.d -MQ foo -MQbar");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MQ_flag_without_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d foo.c");
  struct args* exp_cpp = args_init_from_string("gcc");
  struct args* exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MQ foo.d");
  struct args* exp_cc =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQ foo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MT_flag_without_immediate_argument_should_not_add_MTobj)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d foo.c");
  struct args* exp_cpp = args_init_from_string("gcc");
  struct args* exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MT foo.d");
  struct args* exp_cc =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MT foo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MQ_flag_with_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d foo.c");
  struct args* exp_cpp = args_init_from_string("gcc");
  struct args* exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MQfoo.d");
  struct args* exp_cc =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MQfoo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(MT_flag_with_immediate_argument_should_not_add_MQobj)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d foo.c");
  struct args* exp_cpp = args_init_from_string("gcc");
  struct args* exp_extra = args_init_from_string("-MD -MP -MFfoo.d -MTfoo.d");
  struct args* exp_cc =
    args_init_from_string("gcc -c -MD -MP -MFfoo.d -MTfoo.d");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(fprofile_flag_with_existing_dir_should_be_rewritten_to_real_path)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -fprofile-generate=some/dir foo.c");
  struct args* exp_cpp = args_init_from_string("gcc");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("gcc");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  char *s, *path;

  create_file("foo.c", "");
  mkdir("some", 0777);
  mkdir("some/dir", 0777);
  path = x_realpath("some/dir");
  s = format("-fprofile-generate=%s", path);
  free(path);
  args_add(exp_cpp, s);
  args_add(exp_cc, s);
  args_add(exp_cc, "-c");
  free(s);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(fprofile_flag_with_nonexistent_dir_should_not_be_rewritten)
{
  Context ctx;

  struct args* orig =
    args_init_from_string("gcc -c -fprofile-generate=some/dir foo.c");
  struct args* exp_cpp =
    args_init_from_string("gcc -fprofile-generate=some/dir");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc =
    args_init_from_string("gcc -fprofile-generate=some/dir -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(isystem_flag_with_separate_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  extern char* current_working_dir;
  char* arg_string;
  struct args* orig;
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  g_config.set_base_dir(get_root());
  current_working_dir = get_cwd();
  arg_string = format("cc -isystem %s/foo -c foo.c", current_working_dir);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_STR_EQ("./foo", act_cpp->argv[2]);

  args_free(orig);
  args_free(act_cpp);
  args_free(act_cc);
}

TEST(isystem_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  extern char* current_working_dir;
  char* cwd;
  char* arg_string;
  struct args* orig;
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  g_config.set_base_dir("/"); // posix
  current_working_dir = get_cwd();
  // Windows path doesn't work concatenated.
  cwd = get_posix_path(current_working_dir);
  arg_string = format("cc -isystem%s/foo -c foo.c", cwd);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_STR_EQ("-isystem./foo", act_cpp->argv[1]);

  free(cwd);
  args_free(orig);
  args_free(act_cpp);
  args_free(act_cc);
}

TEST(I_flag_with_concat_arg_should_be_rewritten_if_basedir_is_used)
{
  Context ctx;

  extern char* current_working_dir;
  char* cwd;
  char* arg_string;
  struct args* orig;
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  g_config.set_base_dir(x_strdup("/")); // posix
  current_working_dir = get_cwd();
  // Windows path doesn't work concatenated.
  cwd = get_posix_path(current_working_dir);
  arg_string = format("cc -I%s/foo -c foo.c", cwd);
  orig = args_init_from_string(arg_string);
  free(arg_string);

  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_STR_EQ("-I./foo", act_cpp->argv[1]);

  free(cwd);
  args_free(orig);
  args_free(act_cpp);
  args_free(act_cc);
}

TEST(debug_flag_order_with_known_option_first)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -g1 -gsplit-dwarf foo.c -c");
  struct args* exp_cpp = args_init_from_string("cc -g1 -gsplit-dwarf");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -g1 -gsplit-dwarf -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(debug_flag_order_with_known_option_last)
{
  Context ctx;

  struct args* orig = args_init_from_string("cc -gsplit-dwarf -g1 foo.c -c");
  struct args* exp_cpp = args_init_from_string("cc -gsplit-dwarf -g1");
  struct args* exp_extra = args_init(0, NULL);
  struct args* exp_cc = args_init_from_string("cc -gsplit-dwarf -g1 -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST(options_not_to_be_passed_to_the_preprocesor)
{
  Context ctx;

  struct args* orig = args_init_from_string(
    "cc -Wa,foo foo.c -g -c -DX -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  struct args* exp_cpp = args_init_from_string("cc -g -DX");
  struct args* exp_extra = args_init_from_string(
    " -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error");
  struct args* exp_cc = args_init_from_string(
    "cc -g -Wa,foo -Werror -Xlinker fie -Xlinker,fum -Wno-error -DX -c");
  struct args* act_cpp = NULL;
  struct args* act_extra = NULL;
  struct args* act_cc = NULL;

  create_file("foo.c", "");
  CHECK(cc_process_args(ctx, orig, &act_cpp, &act_extra, &act_cc));
  CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
  CHECK_ARGS_EQ_FREE12(exp_extra, act_extra);
  CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

  args_free(orig);
}

TEST_SUITE_END
