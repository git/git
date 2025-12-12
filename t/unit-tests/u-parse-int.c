#include "unit-test.h"
#include "parse.h"

static void check_int(const char *buf, size_t len,
		      size_t expect_ep_ofs, int expect_errno,
		      int expect_result)
{
	const char *ep;
	int result;
	bool ok = parse_int_from_buf(buf, len, &ep, &result);

	if (expect_errno) {
		cl_assert(!ok);
		cl_assert_equal_i(expect_errno, errno);
		return;
	}

	cl_assert(ok);
	cl_assert_equal_i(expect_result, result);
	cl_assert_equal_i(expect_ep_ofs, ep - buf);
}

static void check_int_str(const char *buf, size_t ofs, int err, int res)
{
	check_int(buf, strlen(buf), ofs, err, res);
}

static void check_int_full(const char *buf, int res)
{
	check_int_str(buf, strlen(buf), 0, res);
}

static void check_int_err(const char *buf, int err)
{
	check_int(buf, strlen(buf), 0, err, 0);
}

void test_parse_int__basic(void)
{
	cl_invoke(check_int_full("0", 0));
	cl_invoke(check_int_full("11", 11));
	cl_invoke(check_int_full("-23", -23));
	cl_invoke(check_int_full("+23", 23));

	cl_invoke(check_int_str("  31337  ", 7, 0, 31337));

	cl_invoke(check_int_err("  garbage", EINVAL));
	cl_invoke(check_int_err("", EINVAL));
	cl_invoke(check_int_err("-", EINVAL));

	cl_invoke(check_int("123", 2, 2, 0, 12));
}

void test_parse_int__range(void)
{
	/*
	 * These assume a 32-bit int. We could avoid that with some
	 * conditionals, but it's probably better for the test to
	 * fail noisily and we can decide how to handle it then.
	 */
	cl_invoke(check_int_full("2147483647", 2147483647));
	cl_invoke(check_int_err("2147483648", ERANGE));
	cl_invoke(check_int_full("-2147483647", -2147483647));
	cl_invoke(check_int_full("-2147483648", -2147483648));
	cl_invoke(check_int_err("-2147483649", ERANGE));
}

static void check_unsigned(const char *buf, uintmax_t max,
			   int expect_errno, uintmax_t expect_result)
{
	const char *ep;
	uintmax_t result;
	bool ok = parse_unsigned_from_buf(buf, strlen(buf), &ep, &result, max);

	if (expect_errno) {
		cl_assert(!ok);
		cl_assert_equal_i(expect_errno, errno);
		return;
	}

	cl_assert(ok);
	cl_assert_equal_s(ep, "");
	/*
	 * Do not use cl_assert_equal_i_fmt(..., PRIuMAX) here. The macro
	 * casts to int under the hood, corrupting the values.
	 */
	clar__assert_equal(CLAR_CURRENT_FILE, CLAR_CURRENT_FUNC,
			   CLAR_CURRENT_LINE,
			   "expect_result != result", 1,
			   "%"PRIuMAX, expect_result, result);
}

void test_parse_int__unsigned(void)
{
	cl_invoke(check_unsigned("4294967295", UINT_MAX, 0, 4294967295U));
	cl_invoke(check_unsigned("1053", 1000, ERANGE, 0));
	cl_invoke(check_unsigned("-17", UINT_MAX, EINVAL, 0));
}
