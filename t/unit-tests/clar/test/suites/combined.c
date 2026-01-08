#include <sys/stat.h>

#include "clar.h"

static int file_size(const char *filename)
{
	struct stat st;

	if (stat(filename, &st) == 0)
		return (int)st.st_size;
	return -1;
}

void test_combined__cleanup(void)
{
	cl_fixture_cleanup("test");

	cl_assert(file_size("test/file") == -1);
}

void test_combined__1(void)
{
	cl_assert(1);
	cl_must_pass(0);  /* 0 == success */
	cl_must_fail(-1); /* <0 == failure */
	cl_must_pass(-1); /* demonstrate a failing call */
}

void test_combined__2(void)
{
	cl_fixture_sandbox("test");

	cl_assert(file_size("test/nonexistent") == -1);
	cl_assert(file_size("test/file") > 0);
	cl_assert(100 == 101);
}

void test_combined__strings(void)
{
	const char *actual = "expected";
	cl_assert_equal_s("expected", actual);
	cl_assert_equal_s_("expected", actual, "second try with annotation");
	cl_assert_equal_s_("mismatched", actual, "this one fails");
}

void test_combined__strings_with_length(void)
{
	const char *actual = "expected";
	cl_assert_equal_strn("expected_", actual, 8);
	cl_assert_equal_strn("exactly", actual, 2);
	cl_assert_equal_strn_("expected_", actual, 8, "with annotation");
	cl_assert_equal_strn_("exactly", actual, 3, "this one fails");
}

void test_combined__int(void)
{
	int value = 100;
	cl_assert_equal_i(101, value);
}

void test_combined__int_note(void)
{
	int value = 100;
	cl_assert_equal_i_(101, value, "extra note on failing test");
}

void test_combined__int_fmt(void)
{
	int value = 100;
	cl_assert_equal_i_fmt(022, value, "%04o");
}

void test_combined__bool(void)
{
	int value = 100;
	cl_assert_equal_b(1, value);       /* test equality as booleans */
	cl_assert_equal_b(0, value);
}

void test_combined__multiline_description(void)
{
	cl_must_pass_(-1, "description line 1\ndescription line 2");
}

void test_combined__null_string(void)
{
	const char *actual = NULL;
	cl_assert_equal_s(actual, actual);
	cl_assert_equal_s_("expected", actual, "this one fails");
}

void test_combined__failf(void)
{
	cl_failf("some reason: %s", "foo");
}

void test_combined__compare_i(void)
{
	int one = 1, two = 2;

	cl_assert_equal_i(one, 1);
	cl_assert_equal_i(one, 1);
	cl_assert_equal_i_(one, 1, "format");
	cl_assert_lt_i(one, 2);
	cl_assert_lt_i_(one, 2, "format");
	cl_assert_le_i(one, 2);
	cl_assert_le_i(two, 2);
	cl_assert_le_i_(two, 2, "format");
	cl_assert_gt_i(two, 1);
	cl_assert_gt_i_(two, 1, "format");
	cl_assert_ge_i(two, 2);
	cl_assert_ge_i(3, two);
	cl_assert_ge_i_(3, two, "format");

	cl_assert_lt_i(two, 1); /* this one fails */
}

void test_combined__compare_i_with_format(void)
{
	int two = 2;
	cl_assert_lt_i_(two, 1, "foo: %s", "bar");
}

void test_combined__compare_u(void)
{
	unsigned one = 1, two = 2;

	cl_assert_equal_u(one, 1);
	cl_assert_equal_u_(one, 1, "format");
	cl_assert_lt_u(one, 2);
	cl_assert_lt_u_(one, 2, "format");
	cl_assert_le_u(one, 2);
	cl_assert_le_u(two, 2);
	cl_assert_le_u_(two, 2, "format");
	cl_assert_gt_u(two, 1);
	cl_assert_gt_u_(two, 1, "format");
	cl_assert_ge_u(two, 2);
	cl_assert_ge_u(3, two);
	cl_assert_ge_u_(3, two, "format");

	cl_assert_lt_u(two, 1); /* this one fails */
}

void test_combined__compare_u_with_format(void)
{
	unsigned two = 2;
	cl_assert_lt_u_(two, 1, "foo: %s", "bar");
}
