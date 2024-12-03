/*
 * Copyright (c) Vicent Marti. All rights reserved.
 *
 * This file is part of clar, distributed under the ISC license.
 * For full terms see the included COPYING file.
 */

#include "clar_test.h"

/*
 * Minimal main() for clar tests.
 *
 * Modify this with any application specific setup or teardown that you need.
 * The only required line is the call to `clar_test(argc, argv)`, which will
 * execute the test suite.  If you want to check the return value of the test
 * application, main() should return the same value returned by clar_test().
 */

#ifdef _WIN32
int __cdecl main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
	/* Run the test suite */
	return clar_test(argc, argv);
}
