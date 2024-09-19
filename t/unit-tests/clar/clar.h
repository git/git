/*
 * Copyright (c) Vicent Marti. All rights reserved.
 *
 * This file is part of clar, distributed under the ISC license.
 * For full terms see the included COPYING file.
 */
#ifndef __CLAR_TEST_H__
#define __CLAR_TEST_H__

#include <stdlib.h>

enum cl_test_status {
	CL_TEST_OK,
	CL_TEST_FAILURE,
	CL_TEST_SKIP,
	CL_TEST_NOTRUN,
};

enum cl_output_format {
	CL_OUTPUT_CLAP,
	CL_OUTPUT_TAP,
};

/** Setup clar environment */
void clar_test_init(int argc, char *argv[]);
int clar_test_run(void);
void clar_test_shutdown(void);

/** One shot setup & run */
int clar_test(int argc, char *argv[]);

const char *clar_sandbox_path(void);

void cl_set_cleanup(void (*cleanup)(void *), void *opaque);
void cl_fs_cleanup(void);

/**
 * cl_trace_* is a hook to provide a simple global tracing
 * mechanism.
 *
 * The goal here is to let main() provide clar-proper
 * with a callback to optionally write log info for
 * test operations into the same stream used by their
 * actual tests.  This would let them print test names
 * and maybe performance data as they choose.
 *
 * The goal is NOT to alter the flow of control or to
 * override test selection/skipping.  (So the callback
 * does not return a value.)
 *
 * The goal is NOT to duplicate the existing
 * pass/fail/skip reporting.  (So the callback
 * does not accept a status/errorcode argument.)
 *
 */
typedef enum cl_trace_event {
	CL_TRACE__SUITE_BEGIN,
	CL_TRACE__SUITE_END,
	CL_TRACE__TEST__BEGIN,
	CL_TRACE__TEST__END,
	CL_TRACE__TEST__RUN_BEGIN,
	CL_TRACE__TEST__RUN_END,
	CL_TRACE__TEST__LONGJMP,
} cl_trace_event;

typedef void (cl_trace_cb)(
	cl_trace_event ev,
	const char *suite_name,
	const char *test_name,
	void *payload);

/**
 * Register a callback into CLAR to send global trace events.
 * Pass NULL to disable.
 */
void cl_trace_register(cl_trace_cb *cb, void *payload);


#ifdef CLAR_FIXTURE_PATH
const char *cl_fixture(const char *fixture_name);
void cl_fixture_sandbox(const char *fixture_name);
void cl_fixture_cleanup(const char *fixture_name);
const char *cl_fixture_basename(const char *fixture_name);
#endif

/**
 * Assertion macros with explicit error message
 */
#define cl_must_pass_(expr, desc) clar__assert((expr) >= 0, __FILE__, __func__, __LINE__, "Function call failed: " #expr, desc, 1)
#define cl_must_fail_(expr, desc) clar__assert((expr) < 0, __FILE__, __func__, __LINE__, "Expected function call to fail: " #expr, desc, 1)
#define cl_assert_(expr, desc) clar__assert((expr) != 0, __FILE__, __func__, __LINE__, "Expression is not true: " #expr, desc, 1)

/**
 * Check macros with explicit error message
 */
#define cl_check_pass_(expr, desc) clar__assert((expr) >= 0, __FILE__, __func__, __LINE__, "Function call failed: " #expr, desc, 0)
#define cl_check_fail_(expr, desc) clar__assert((expr) < 0, __FILE__, __func__, __LINE__, "Expected function call to fail: " #expr, desc, 0)
#define cl_check_(expr, desc) clar__assert((expr) != 0, __FILE__, __func__, __LINE__, "Expression is not true: " #expr, desc, 0)

/**
 * Assertion macros with no error message
 */
#define cl_must_pass(expr) cl_must_pass_(expr, NULL)
#define cl_must_fail(expr) cl_must_fail_(expr, NULL)
#define cl_assert(expr) cl_assert_(expr, NULL)

/**
 * Check macros with no error message
 */
#define cl_check_pass(expr) cl_check_pass_(expr, NULL)
#define cl_check_fail(expr) cl_check_fail_(expr, NULL)
#define cl_check(expr) cl_check_(expr, NULL)

/**
 * Forced failure/warning
 */
#define cl_fail(desc) clar__fail(__FILE__, __func__, __LINE__, "Test failed.", desc, 1)
#define cl_warning(desc) clar__fail(__FILE__, __func__, __LINE__, "Warning during test execution:", desc, 0)

#define cl_skip() clar__skip()

/**
 * Typed assertion macros
 */
#define cl_assert_equal_s(s1,s2) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #s1 " != " #s2, 1, "%s", (s1), (s2))
#define cl_assert_equal_s_(s1,s2,note) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #s1 " != " #s2 " (" #note ")", 1, "%s", (s1), (s2))

#define cl_assert_equal_wcs(wcs1,wcs2) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #wcs1 " != " #wcs2, 1, "%ls", (wcs1), (wcs2))
#define cl_assert_equal_wcs_(wcs1,wcs2,note) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #wcs1 " != " #wcs2 " (" #note ")", 1, "%ls", (wcs1), (wcs2))

#define cl_assert_equal_strn(s1,s2,len) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #s1 " != " #s2, 1, "%.*s", (s1), (s2), (int)(len))
#define cl_assert_equal_strn_(s1,s2,len,note) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #s1 " != " #s2 " (" #note ")", 1, "%.*s", (s1), (s2), (int)(len))

#define cl_assert_equal_wcsn(wcs1,wcs2,len) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #wcs1 " != " #wcs2, 1, "%.*ls", (wcs1), (wcs2), (int)(len))
#define cl_assert_equal_wcsn_(wcs1,wcs2,len,note) clar__assert_equal(__FILE__,__func__,__LINE__,"String mismatch: " #wcs1 " != " #wcs2 " (" #note ")", 1, "%.*ls", (wcs1), (wcs2), (int)(len))

#define cl_assert_equal_i(i1,i2) clar__assert_equal(__FILE__,__func__,__LINE__,#i1 " != " #i2, 1, "%d", (int)(i1), (int)(i2))
#define cl_assert_equal_i_(i1,i2,note) clar__assert_equal(__FILE__,__func__,__LINE__,#i1 " != " #i2 " (" #note ")", 1, "%d", (i1), (i2))
#define cl_assert_equal_i_fmt(i1,i2,fmt) clar__assert_equal(__FILE__,__func__,__LINE__,#i1 " != " #i2, 1, (fmt), (int)(i1), (int)(i2))

#define cl_assert_equal_b(b1,b2) clar__assert_equal(__FILE__,__func__,__LINE__,#b1 " != " #b2, 1, "%d", (int)((b1) != 0),(int)((b2) != 0))

#define cl_assert_equal_p(p1,p2) clar__assert_equal(__FILE__,__func__,__LINE__,"Pointer mismatch: " #p1 " != " #p2, 1, "%p", (p1), (p2))

void clar__skip(void);

void clar__fail(
	const char *file,
	const char *func,
	size_t line,
	const char *error,
	const char *description,
	int should_abort);

void clar__assert(
	int condition,
	const char *file,
	const char *func,
	size_t line,
	const char *error,
	const char *description,
	int should_abort);

void clar__assert_equal(
	const char *file,
	const char *func,
	size_t line,
	const char *err,
	int should_abort,
	const char *fmt,
	...);

#endif
