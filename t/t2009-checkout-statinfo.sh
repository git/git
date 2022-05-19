#!/bin/sh

test_description='checkout should leave clean stat info'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '

	echo hello >world &&
	but update-index --add world &&
	but cummit -m initial &&
	but branch side &&
	echo goodbye >world &&
	but update-index --add world &&
	but cummit -m second

'

test_expect_success 'branch switching' '

	but reset --hard &&
	test "$(but diff-files --raw)" = "" &&

	but checkout main &&
	test "$(but diff-files --raw)" = "" &&

	but checkout side &&
	test "$(but diff-files --raw)" = "" &&

	but checkout main &&
	test "$(but diff-files --raw)" = ""

'

test_expect_success 'path checkout' '

	but reset --hard &&
	test "$(but diff-files --raw)" = "" &&

	but checkout main world &&
	test "$(but diff-files --raw)" = "" &&

	but checkout side world &&
	test "$(but diff-files --raw)" = "" &&

	but checkout main world &&
	test "$(but diff-files --raw)" = ""

'

test_done

