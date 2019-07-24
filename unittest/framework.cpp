// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

#include "framework.hpp"
#include "util.hpp"

#include <float.h>
#include <math.h>
#if defined(HAVE_TERMIOS_H)
#define USE_COLOR
#include <termios.h>
#endif

static unsigned total_asserts;
static unsigned total_tests;
static unsigned total_suites;
static unsigned failed_tests;
static const char *current_suite;
static const char *current_test;
static char *dir_before_suite;
static char *dir_before_test;
static int verbose;

static const char COLOR_END[] = "\x1b[m";
static const char COLOR_GREEN[] = "\x1b[1;32m";
static const char COLOR_RED[] = "\x1b[1;31m";
static char CONFIG_PATH_ENV[] = "CCACHE_CONFIG_PATH=/dev/null";

#define COLOR(tty, color) ((tty) ? COLOR_ ## color : "")

static int
is_tty(int fd)
{
#ifdef USE_COLOR
	struct termios t;
	return tcgetattr(fd, &t) == 0;
#else
	(void)fd;
	return 0;
#endif
}

static const char *
plural_s(unsigned n)
{
	return n == 1 ? "" : "s";
}

int
cct_run(suite_fn *suites, int verbose_output)
{
	suite_fn *suite;
	int tty = is_tty(1);

	x_unsetenv("GCC_COLORS"); // Avoid confusing argument processing tests.
	verbose = verbose_output;

	for (suite = suites; *suite; suite++) {
		unsigned test_index = 0;
		while (true) {
			test_index = (*suite)(test_index + 1);
			if (test_index == 0) {
				// We have reached the end of the suite.
				break;
			}
		}
	}

	if (failed_tests == 0) {
		printf("%sPASSED%s: %u assertion%s, %u test%s, %u suite%s\n",
		       COLOR(tty, GREEN), COLOR(tty, END),
		       total_asserts, plural_s(total_asserts),
		       total_tests, plural_s(total_tests),
		       total_suites, plural_s(total_suites));
	} else {
		printf("%sFAILED%s: %u test%s\n",
		       COLOR(tty, RED), COLOR(tty, END),
		       failed_tests, plural_s(failed_tests));
	}
	return failed_tests > 0 ? 1 : 0;
}

void
cct_suite_begin(const char *name)
{
	++total_suites;
	if (verbose) {
		printf("=== SUITE: %s ===\n", name);
	}
	dir_before_suite = gnu_getcwd();
	create_dir(name);
	cct_chdir(name);
	current_suite = name;
}

void
cct_suite_end()
{
	cct_chdir(dir_before_suite);
	free(dir_before_suite);
	dir_before_suite = NULL;
}

void
cct_test_begin(const char *name)
{
	++total_tests;
	if (verbose) {
		printf("--- TEST: %s ---\n", name);
	}
	dir_before_test = gnu_getcwd();
	create_dir(name);
	cct_chdir(name);
	current_test = name;

	putenv(CONFIG_PATH_ENV);
	cc_reset();
}

void
cct_test_end()
{
	if (dir_before_test) {
		cct_chdir(dir_before_test);
		free(dir_before_test);
		dir_before_test = NULL;
	}
}

void
cct_check_passed(const char *file, int line, const char *what)
{
	++total_asserts;
	if (verbose) {
		printf("%s:%d: Passed assertion: %s\n", file, line, what);
	}
}

void
cct_check_failed(const char *file, int line, const char *what,
                 const char *expected, const char *actual)
{
	++total_asserts;
	++failed_tests;
	fprintf(stderr, "%s:%d: Failed assertion:\n", file, line);
	fprintf(stderr, "  Suite:      %s\n", current_suite);
	fprintf(stderr, "  Test:       %s\n", current_test);
	if (expected) {
		fprintf(stderr, "  Expression: %s\n", what);
		if (actual) {
			fprintf(stderr, "  Expected:   %s\n", expected);
			fprintf(stderr, "  Actual:     %s\n", actual);
		} else {
			fprintf(stderr, "  Message:    %s\n", expected);
		}
	} else {
		fprintf(stderr, "  Assertion:  %s\n", what);
	}
	fprintf(stderr, "\n");
}

bool
cct_check_double_eq(const char *file, int line, const char *expression,
                    double expected, double actual)
{
	if (fabs(expected -  actual) < DBL_EPSILON) {
		cct_check_passed(file, line, expression);
		return true;
	} else {
		char *exp_str = format("%.1f", expected);
		char *act_str = format("%.1f", actual);
		cct_check_failed(file, line, expression, exp_str, act_str);
		free(exp_str);
		free(act_str);
		return false;
	}
}
bool
cct_check_int_eq(const char *file, int line, const char *expression,
                 int64_t expected, int64_t actual)
{
	if (expected == actual) {
		cct_check_passed(file, line, expression);
		return true;
	} else {
#if defined(HAVE_LONG_LONG) && !defined(__MINGW32__)
		char *exp_str = format("%lld", (long long)expected);
		char *act_str = format("%lld", (long long)actual);
#else
		char *exp_str = format("%ld", (long)expected);
		char *act_str = format("%ld", (long)actual);
#endif
		cct_check_failed(file, line, expression, exp_str, act_str);
		free(exp_str);
		free(act_str);
		return false;
	}
}

bool cct_check_data_eq(const char *file, int line, const char *expression,
                       const uint8_t *expected, const uint8_t *actual,
                       size_t size)
{
	if (memcmp(actual, expected, size) == 0) {
		cct_check_passed(file, line, expression);
		return true;
	} else {
		char *exp_str = static_cast<char*>(x_malloc(2 * size + 1));
		char *act_str = static_cast<char*>(x_malloc(2 * size + 1));
		format_hex(expected, size, exp_str);
		format_hex(actual, size, act_str);
		cct_check_failed(file, line, expression, exp_str, act_str);
		free(exp_str);
		free(act_str);
		return false;
	}
}

bool
cct_check_str_eq(const char *file, int line, const char *expression,
                 const char *expected, const char *actual,
                 bool free1, bool free2)
{
	bool result;

	if (expected && actual && str_eq(actual, expected)) {
		cct_check_passed(file, line, expression);
		result = true;
	} else {
		char *exp_str = expected ? format("\"%s\"", expected) : x_strdup("(null)");
		char *act_str = actual ? format("\"%s\"", actual) : x_strdup("(null)");
		cct_check_failed(file, line, expression, exp_str, act_str);
		free(exp_str);
		free(act_str);
		result = false;
	}

	if (free1) {
		free(const_cast<char*>(expected));
	}
	if (free2) {
		free(const_cast<char*>(actual));
	}
	return result;
}

bool
cct_check_args_eq(const char *file, int line, const char *expression,
                  const struct args *expected, const struct args *actual,
                  bool free1, bool free2)
{
	bool result;

	if (expected && actual && args_equal(actual, expected)) {
		cct_check_passed(file, line, expression);
		result = true;
	} else {
		char *exp_str = expected ? args_to_string(expected) : x_strdup("(null)");
		char *act_str = actual ? args_to_string(actual) : x_strdup("(null)");
		cct_check_failed(file, line, expression, exp_str, act_str);
		free(exp_str);
		free(act_str);
		result = false;
	}

	if (free1) {
		args_free(const_cast<struct args*>(expected));
	}
	if (free2) {
		args_free(const_cast<struct args*>(actual));
	}
	return result;
}

void
cct_chdir(const char *path)
{
	if (chdir(path) != 0) {
		fprintf(stderr, "chdir: %s: %s", path, strerror(errno));
		abort();
	}
}

void
cct_wipe(const char *path)
{
	// TODO: rewrite using traverse().
#ifndef __MINGW32__
	char *command = format("rm -rf %s", path);
#else
	char *command = format("rd /s /q %s", path);
#endif
	if (system(command) != 0) {
		perror(command);
	}
	free(command);
}

void
cct_create_fresh_dir(const char *path)
{
	cct_wipe(path);
	if (mkdir(path, 0777) != 0) {
		fprintf(stderr, "mkdir: %s: %s", path, strerror(errno));
		abort();
	}
}
