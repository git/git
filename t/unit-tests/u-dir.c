#include "unit-test.h"
#include "dir.h"

#define TEST_WITHIN_DEPTH(path, depth, max_depth, expect) do { \
		int actual = within_depth(path, strlen(path), \
					  depth, max_depth); \
		if (actual != expect) \
			cl_failf("path '%s' with depth '%d' and max-depth '%d': expected %d, got %d", \
				 path, depth, max_depth, expect, actual); \
	} while (0)

void test_dir__within_depth(void)
{
	/* depth = 0; max_depth = 0 */
	TEST_WITHIN_DEPTH("",         0, 0, 1);
	TEST_WITHIN_DEPTH("file",     0, 0, 1);
	TEST_WITHIN_DEPTH("a",        0, 0, 1);
	TEST_WITHIN_DEPTH("a/file",   0, 0, 0);
	TEST_WITHIN_DEPTH("a/b",      0, 0, 0);
	TEST_WITHIN_DEPTH("a/b/file", 0, 0, 0);

	/* depth = 0; max_depth = 1 */
	TEST_WITHIN_DEPTH("",         0, 1, 1);
	TEST_WITHIN_DEPTH("file",     0, 1, 1);
	TEST_WITHIN_DEPTH("a",        0, 1, 1);
	TEST_WITHIN_DEPTH("a/file",   0, 1, 1);
	TEST_WITHIN_DEPTH("a/b",      0, 1, 1);
	TEST_WITHIN_DEPTH("a/b/file", 0, 1, 0);

	/* depth = 1; max_depth = 1 */
	TEST_WITHIN_DEPTH("",         1, 1, 1);
	TEST_WITHIN_DEPTH("file",     1, 1, 1);
	TEST_WITHIN_DEPTH("a",        1, 1, 1);
	TEST_WITHIN_DEPTH("a/file",   1, 1, 0);
	TEST_WITHIN_DEPTH("a/b",      1, 1, 0);
	TEST_WITHIN_DEPTH("a/b/file", 1, 1, 0);

	/* depth = 1; max_depth = 0 */
	TEST_WITHIN_DEPTH("",         1, 0, 0);
	TEST_WITHIN_DEPTH("file",     1, 0, 0);
	TEST_WITHIN_DEPTH("a",        1, 0, 0);
	TEST_WITHIN_DEPTH("a/file",   1, 0, 0);
	TEST_WITHIN_DEPTH("a/b",      1, 0, 0);
	TEST_WITHIN_DEPTH("a/b/file", 1, 0, 0);


}
