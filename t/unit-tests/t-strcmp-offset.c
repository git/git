#include "test-lib.h"
#include "read-cache-ll.h"

static void check_strcmp_offset(const char *string1, const char *string2,
				int expect_result, uintmax_t expect_offset)
{
	size_t offset;
	int result = strcmp_offset(string1, string2, &offset);

	/*
	 * Because different CRTs behave differently, only rely on signs of the
	 * result values.
	 */
	result = (result < 0 ? -1 :
			result > 0 ? 1 :
			0);

	check_int(result, ==, expect_result);
	check_uint((uintmax_t)offset, ==, expect_offset);
}

#define TEST_STRCMP_OFFSET(string1, string2, expect_result, expect_offset) \
	TEST(check_strcmp_offset(string1, string2, expect_result,          \
				 expect_offset),                           \
	     "strcmp_offset(%s, %s) works", #string1, #string2)

int cmd_main(int argc, const char **argv)
{
	TEST_STRCMP_OFFSET("abc", "abc", 0, 3);
	TEST_STRCMP_OFFSET("abc", "def", -1, 0);
	TEST_STRCMP_OFFSET("abc", "abz", -1, 2);
	TEST_STRCMP_OFFSET("abc", "abcdef", -1, 3);

	return test_done();
}
