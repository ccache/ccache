/*
 * Copyright (C) 2010 Joel Rosdahl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * This file contains tests for the processing of compiler arguments.
 */

#include "ccache.h"
#include "test/framework.h"
#include "test/util.h"

TEST_SUITE(argument_processing)

TEST(dash_E_should_result_in_called_for_preprocessing)
{
	struct args *orig = args_init_from_string("cc -c foo.c -E");
	struct args *preprocessed, *compiler;

	create_file("foo.c", "");
	CHECK(!cc_process_args(orig, &preprocessed, &compiler));
	CHECK_UNS_EQ(1, stats_get_pending(STATS_PREPROCESSING));

	args_free(orig);
}

TEST(dash_M_should_be_unsupported)
{
	struct args *orig = args_init_from_string("cc -c foo.c -M");
	struct args *preprocessed, *compiler;

	create_file("foo.c", "");
	CHECK(!cc_process_args(orig, &preprocessed, &compiler));
	CHECK_UNS_EQ(1, stats_get_pending(STATS_UNSUPPORTED));

	args_free(orig);
}

TEST(dependency_flags_should_only_be_sent_to_the_preprocessor)
{
#define CMD \
	"cc -c -MD -MMD -MP -MF foo.d -MT mt1 -MT mt2 -MQ mq1 -MQ mq2" \
	" -Wp,-MD,wpmd -Wp,-MMD,wpmmd"
	struct args *orig = args_init_from_string(CMD " foo.c -o foo.o");
	struct args *exp_cpp = args_init_from_string(CMD);
#undef CMD
	struct args *exp_cc = args_init_from_string("cc -c");
	struct args *act_cpp = NULL, *act_cc = NULL;
	create_file("foo.c", "");

	CHECK(cc_process_args(orig, &act_cpp, &act_cc));
	CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
	CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

	args_free(orig);
}

TEST(dependency_flags_that_take_an_argument_should_not_require_space_delimiter)
{
	struct args *orig = args_init_from_string(
		"cc -c -MMD -MFfoo.d -MTmt -MQmq foo.c -o foo.o");
	struct args *exp_cpp = args_init_from_string(
		"cc -c -MMD -MFfoo.d -MTmt -MQmq");
	struct args *exp_cc = args_init_from_string("cc -c");
	struct args *act_cpp = NULL, *act_cc = NULL;
	create_file("foo.c", "");

	CHECK(cc_process_args(orig, &act_cpp, &act_cc));
	CHECK_ARGS_EQ_FREE12(exp_cpp, act_cpp);
	CHECK_ARGS_EQ_FREE12(exp_cc, act_cc);

	args_free(orig);
}

TEST_SUITE_END
