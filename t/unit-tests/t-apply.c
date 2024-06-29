#include "test-lib.h"
#include "apply.h"

#define FAILURE -1

typedef struct test_case {
	const char *line;
	const char *expect_suffix;
	int offset;
	unsigned long expect_p1;
	unsigned long expect_p2;
	int expect_result;
} test_case;

static void setup_static(struct test_case t)
{
	unsigned long p1 = 9999;
	unsigned long p2 = 9999;
	int result = apply_parse_fragment_range(t.line, strlen(t.line), t.offset,
						t.expect_suffix, &p1, &p2);
	check_int(result, ==, t.expect_result);
	check_int(p1, ==, t.expect_p1);
	check_int(p2, ==, t.expect_p2);
}

int cmd_main(int argc, const char **argv)
{
	TEST(setup_static((struct test_case) {
		.line = "@@ -4,4 +",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = 9,
		.expect_p1 = 4,
		.expect_p2 = 4
	}), "well-formed range");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4 +8 @@",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = 7,
		.expect_p1 = 4,
		.expect_p2 = 1
	}), "non-comma range");

	TEST(setup_static((struct test_case) {
		.line = "@@ -X,4 +",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 9999,
		.expect_p2 = 9999
	}), "non-digit range (first coordinate)");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4,X +",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 4,
		.expect_p2 = 1 // A little strange this is 1, but not end of the world
	}), "non-digit range (second coordinate)");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4,4 -",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 4,
		.expect_p2 = 4
	}), "non-expected trailing text");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4,4",
		.offset = 4,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 4,
		.expect_p2 = 4
	}), "not long enough for expected trailing text");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4,4",
		.offset = 7,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 9999,
		.expect_p2 = 9999
	}), "not long enough for offset");

	TEST(setup_static((struct test_case) {
		.line = "@@ -4,4",
		.offset = -1,
		.expect_suffix = " +",
		.expect_result = FAILURE,
		.expect_p1 = 9999,
		.expect_p2 = 9999
	}), "negative offset");

	return test_done();
}
