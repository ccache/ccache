// Copyright (C) 2010-2018 Joel Rosdahl
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

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include "../src/ccache.h"

// ============================================================================

#define TEST_SUITE(name) \
	unsigned suite_##name(unsigned _start_point); \
	unsigned suite_##name(unsigned _start_point) \
	{ \
		unsigned _test_counter = 0; \
		cct_suite_begin(#name); \
		{ \
			// Empty due to macro trickery.

#define TEST(name) \
			cct_test_end(); \
		} \
		++_test_counter; \
		{ static int name = 0; (void)name; /* Verify test name. */ } \
		if (_test_counter >= _start_point) { \
			cct_test_begin(#name);

#define TEST_SUITE_END \
			cct_test_end(); \
		} \
		cct_suite_end(); \
		return 0; /* We have reached the end. */ \
	}

// ============================================================================

#define CHECKM(assertion, message) \
	do { \
		if ((assertion)) { \
			cct_check_passed(__FILE__, __LINE__, #assertion); \
		} else { \
			cct_check_failed(__FILE__, __LINE__, #assertion, (message), NULL); \
			cct_test_end(); \
			cct_suite_end(); \
			return _test_counter; \
		} \
	} while (false)

#define CHECK(assertion) \
	CHECKM(assertion, NULL)

#define CHECK_POINTER_EQ_BASE(t, e, a, f1, f2) \
	do { \
		if (!cct_check_##t##_eq(__FILE__, __LINE__, #a, (e), (a), (f1), (f2))) { \
			cct_test_end(); \
			cct_suite_end(); \
			return _test_counter; \
		} \
	} while (false)

// ============================================================================

#define CHECK_INT_EQ(expected, actual) \
	do { \
		if (!cct_check_int_eq(__FILE__, __LINE__, #actual, (expected), \
		                      (actual))) { \
			cct_test_end(); \
			cct_suite_end(); \
			return _test_counter; \
		} \
	} while (false)

// ============================================================================

#define CHECK_DOUBLE_EQ(expected, actual) \
	do { \
		if (!cct_check_double_eq(__FILE__, __LINE__, #actual, (expected), \
		                         (actual))) { \
			cct_test_end(); \
			cct_suite_end(); \
			return _test_counter; \
		} \
	} while (false)

// ============================================================================

#define CHECK_STR_EQ(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, false, false)

#define CHECK_STR_EQ_FREE1(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, true, false)

#define CHECK_STR_EQ_FREE2(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, false, true)

#define CHECK_STR_EQ_FREE12(expected, actual) \
	CHECK_POINTER_EQ_BASE(str, expected, actual, true, true)

// ============================================================================

#define CHECK_ARGS_EQ(expected, actual) \
	CHECK_POINTER_EQ_BASE(args, expected, actual, false, false)

#define CHECK_ARGS_EQ_FREE1(expected, actual) \
	CHECK_POINTER_EQ_BASE(args, expected, actual, true, false)

#define CHECK_ARGS_EQ_FREE2(expected, actual) \
	CHECK_POINTER_EQ_BASE(args, expected, actual, false, true)

#define CHECK_ARGS_EQ_FREE12(expected, actual) \
	CHECK_POINTER_EQ_BASE(args, expected, actual, true, true)

// ============================================================================

typedef unsigned (*suite_fn)(unsigned);
int cct_run(suite_fn *suites, int verbose);

void cct_suite_begin(const char *name);
void cct_suite_end(void);
void cct_test_begin(const char *name);
void cct_test_end(void);
void cct_check_passed(const char *file, int line, const char *assertion);
void cct_check_failed(const char *file, int line, const char *assertion,
                      const char *expected, const char *actual);
bool cct_check_double_eq(const char *file, int line, const char *expression,
                         double expected, double actual);
bool cct_check_int_eq(const char *file, int line, const char *expression,
                      int64_t expected, int64_t actual);
bool cct_check_str_eq(const char *file, int line, const char *expression,
                      char *expected, char *actual,
		      bool free1, bool free2);
bool cct_check_args_eq(const char *file, int line, const char *expression,
                       struct args *expected, struct args *actual,
                       bool free1, bool free2);
void cct_chdir(const char *path);
void cct_wipe(const char *path);
void cct_create_fresh_dir(const char *path);

#endif
