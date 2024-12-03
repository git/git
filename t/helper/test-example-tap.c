#include "test-tool.h"
#include "t/unit-tests/test-lib.h"

/*
 * The purpose of this "unit test" is to verify a few invariants of the unit
 * test framework itself, as well as to provide examples of output from actually
 * failing tests. As such, it is intended that this test fails, and thus it
 * should not be run as part of `make unit-tests`. Instead, we verify it behaves
 * as expected in the integration test t0080-unit-test-output.sh
 */

/* Used to store the return value of check_int(). */
static int check_res;

/* Used to store the return value of TEST(). */
static int test_res;

static void t_res(int expect)
{
	check_int(check_res, ==, expect);
	check_int(test_res, ==, expect);
}

static void t_todo(int x)
{
	check_res = TEST_TODO(check(x));
}

static void t_skip(void)
{
	check(0);
	test_skip("missing prerequisite");
	check(1);
}

static int do_skip(void)
{
	test_skip("missing prerequisite");
	return 1;
}

static void t_skip_todo(void)
{
	check_res = TEST_TODO(do_skip());
}

static void t_todo_after_fail(void)
{
	check(0);
	TEST_TODO(check(0));
}

static void t_fail_after_todo(void)
{
	check(1);
	TEST_TODO(check(0));
	check(0);
}

static void t_messages(void)
{
	check_str("\thello\\", "there\"\n");
	check_str("NULL", NULL);
	check_char('a', ==, '\n');
	check_char('\\', ==, '\'');
}

static void t_empty(void)
{
	; /* empty */
}

int cmd__example_tap(int argc UNUSED, const char **argv UNUSED)
{
	check(1);

	test_res = TEST(check_res = check_int(1, ==, 1), "passing test");
	TEST(t_res(1), "passing test and assertion return 1");
	test_res = TEST(check_res = check_int(1, ==, 2), "failing test");
	TEST(t_res(0), "failing test and assertion return 0");
	test_res = TEST(t_todo(0), "passing TEST_TODO()");
	TEST(t_res(1), "passing TEST_TODO() returns 1");
	test_res = TEST(t_todo(1), "failing TEST_TODO()");
	TEST(t_res(0), "failing TEST_TODO() returns 0");
	test_res = TEST(t_skip(), "test_skip()");
	TEST(check_int(test_res, ==, 1), "skipped test returns 1");
	test_res = TEST(t_skip_todo(), "test_skip() inside TEST_TODO()");
	TEST(t_res(1), "test_skip() inside TEST_TODO() returns 1");
	test_res = TEST(t_todo_after_fail(), "TEST_TODO() after failing check");
	TEST(check_int(test_res, ==, 0), "TEST_TODO() after failing check returns 0");
	test_res = TEST(t_fail_after_todo(), "failing check after TEST_TODO()");
	TEST(check_int(test_res, ==, 0), "failing check after TEST_TODO() returns 0");
	TEST(t_messages(), "messages from failing string and char comparison");
	test_res = TEST(t_empty(), "test with no checks");
	TEST(check_int(test_res, ==, 0), "test with no checks returns 0");

	if_test ("if_test passing test")
		check_int(1, ==, 1);
	if_test ("if_test failing test")
		check_int(1, ==, 2);
	if_test ("if_test passing TEST_TODO()")
		TEST_TODO(check(0));
	if_test ("if_test failing TEST_TODO()")
		TEST_TODO(check(1));
	if_test ("if_test test_skip()") {
		check(0);
		test_skip("missing prerequisite");
		check(1);
	}
	if_test ("if_test test_skip() inside TEST_TODO()")
		TEST_TODO((test_skip("missing prerequisite"), 1));
	if_test ("if_test TEST_TODO() after failing check") {
		check(0);
		TEST_TODO(check(0));
	}
	if_test ("if_test failing check after TEST_TODO()") {
		check(1);
		TEST_TODO(check(0));
		check(0);
	}
	if_test ("if_test messages from failing string and char comparison") {
		check_str("\thello\\", "there\"\n");
		check_str("NULL", NULL);
		check_char('a', ==, '\n');
		check_char('\\', ==, '\'');
	}
	if_test ("if_test test with no checks")
		; /* nothing */

	return test_done();
}
