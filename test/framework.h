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

#define CHECK_POINTER_EQ_BASE(t, e, a, f1, f2)        \
	do { \
		if (cct_check_##t##_eq(__FILE__, __LINE__, #a, (e), (a), (f1), (f2))) { \
			return _test_counter; \
		} \
	} while (0)

/*****************************************************************************/

#define CHECK_INT_EQ(expected, actual) \
	do { \
		if (cct_check_int_eq(__FILE__, __LINE__, #actual, (expected), (actual))) { \
			return _test_counter; \
		} \
	} while (0)

#define CHECK_UNS_EQ(expected, actual) \
	do { \
		if (cct_check_int_eq(__FILE__, __LINE__, #actual, (expected), (actual))) { \
			return _test_counter; \
		} \
	} while (0)

/*****************************************************************************/

#define CHECK_STR_EQ(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, 0, 0)

#define CHECK_STR_EQ_FREE1(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, 1, 0)

#define CHECK_STR_EQ_FREE2(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, 0, 1)

#define CHECK_STR_EQ_FREE12(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, 1, 1)


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
int cct_check_int_eq(const char *file, int line, const char *expression,
                     int expected, int actual);
int cct_check_uns_eq(const char *file, int line, const char *expression,
                     unsigned expected, unsigned actual);
int cct_check_str_eq(const char *file, int line, const char *expression,
                     char *expected, char *actual, int free1, int free2);

#endif
