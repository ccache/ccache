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

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <string.h>
#include <stdlib.h>

/*****************************************************************************/

#define TEST_SUITE(name) \
	unsigned suite_##name(unsigned _start_point) \
	{ \
		unsigned _test_counter = 0; \
		cct_suite_begin(#name); \
		{ \
			/* Empty due to macro trickery. */

#define TEST(name) \
			cct_test_end(); \
		} \
		++_test_counter; \
		if (_test_counter >= _start_point) { \
			cct_test_begin(#name);

#define TEST_SUITE_END \
			cct_test_end(); \
		} \
		cct_suite_end(); \
		return 0; /* We have reached the end. */ \
	}

/*****************************************************************************/

#define CHECK(assertion) \
	do { \
		int ok = (assertion); \
		if (ok) { \
			cct_check_passed(); \
		} else { \
			cct_check_failed(__FILE__, __LINE__, #assertion, NULL, NULL); \
			return _test_counter; \
		} \
	} while (0)

#define CHECK_UNS_EQ(expected, actual) \
	do { \
		unsigned exp = (expected); \
		unsigned act = (actual); \
		if (exp == act) { \
			cct_check_passed(); \
		} else { \
			char *exp_str, *act_str; \
			x_asprintf(&exp_str, "%u", exp); \
			x_asprintf(&act_str, "%u", act); \
			cct_check_failed(__FILE__, __LINE__, #actual, exp_str, act_str); \
			free(exp_str); \
			free(act_str); \
			return _test_counter; \
		} \
	} while (0)

#define CHECK_INT_EQ(expected, actual) \
	do { \
		int exp = (expected); \
		int act = (actual); \
		if (exp == act) { \
			cct_check_passed(); \
		} else { \
			char *exp_str, *act_str; \
			x_asprintf(&exp_str, "%u", exp); \
			x_asprintf(&act_str, "%u", act); \
			cct_check_failed(__FILE__, __LINE__, #actual, exp_str, act_str); \
			free(exp_str); \
			free(act_str); \
			return _test_counter; \
		} \
	} while (0)

#define CHECK_STR_EQ_BASE(expected, actual, free1, free2) \
	do { \
		char *exp = (expected); \
		char *act = (actual); \
		if (exp && act && strcmp(act, exp) == 0) { \
			cct_check_passed(); \
			if (free1) { \
				free(exp); \
			} \
			if (free2) { \
				free(act); \
			} \
		} else { \
			char *exp_str, *act_str; \
			if (exp) { \
				x_asprintf(&exp_str, "\"%s\"", exp); \
			} else { \
				exp_str = x_strdup("(null)"); \
			} \
			if (act) { \
				x_asprintf(&act_str, "\"%s\"", act); \
			} else { \
				act_str = x_strdup("(null)"); \
			} \
			cct_check_failed(__FILE__, __LINE__, #actual, exp_str, act_str); \
			free(exp_str); \
			free(act_str); \
			return _test_counter; \
		} \
	} while (0)

#define CHECK_STR_EQ(expected, actual) \
	CHECK_STR_EQ_BASE(expected, actual, 0, 0)

#define CHECK_STR_EQ_FREE1(expected, actual) \
	CHECK_STR_EQ_BASE(expected, actual, 1, 0)

#define CHECK_STR_EQ_FREE2(expected, actual) \
	CHECK_STR_EQ_BASE(expected, actual, 0, 1)

#define CHECK_STR_EQ_FREE12(expected, actual) \
	CHECK_STR_EQ_BASE(expected, actual, 1, 1)

/*****************************************************************************/

typedef unsigned (*suite_fn)(unsigned);
int cct_run(suite_fn *suites, int verbose);

void cct_suite_begin(const char *name);
void cct_suite_end();
void cct_test_begin(const char *name);
void cct_test_end();
void cct_check_passed(void);
void cct_check_failed(const char *file, int line, const char *assertion,
                      const char *expected, const char *actual);

#endif
