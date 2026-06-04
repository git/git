#include "unit-test.h"
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

	cl_assert_equal_i(result, expect_result);
	cl_assert_equal_i((uintmax_t)offset, expect_offset);
}

void test_strcmp_offset__empty(void)
{
	check_strcmp_offset("", "", 0, 0);
}

void test_strcmp_offset__equal(void)
{
	check_strcmp_offset("abc", "abc", 0, 3);
}

void test_strcmp_offset__different(void)
{
	check_strcmp_offset("abc", "def", -1, 0);
}

void test_strcmp_offset__mismatch(void)
{
	check_strcmp_offset("abc", "abz", -1, 2);
}

void test_strcmp_offset__different_length(void)
{
	check_strcmp_offset("abc", "abcdef", -1, 3);
}
