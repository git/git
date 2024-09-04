/*
 * Copyright (c) Vicent Marti. All rights reserved.
 *
 * This file is part of clar, distributed under the ISC license.
 * For full terms see the included COPYING file.
 */

#include "clar_test.h"

/*
 * Sample main() for clar tests.
 *
 * You should write your own main routine for clar tests that does specific
 * setup and teardown as necessary for your application.  The only required
 * line is the call to `clar_test(argc, argv)`, which will execute the test
 * suite.  If you want to check the return value of the test application,
 * your main() should return the same value returned by clar_test().
 */

int global_test_counter = 0;

#ifdef _WIN32
int __cdecl main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
	int ret;

	/* Your custom initialization here */
	global_test_counter = 0;

	/* Run the test suite */
	ret = clar_test(argc, argv);

	/* Your custom cleanup here */
	cl_assert_equal_i(8, global_test_counter);

	return ret;
}
