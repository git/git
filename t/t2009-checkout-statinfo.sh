#!/bin/sh

test_description='checkout should leave clean stat info'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '

	echo hello >world &&
	git update-index --add world &&
	git commit -m initial &&
	git branch side &&
	echo goodbye >world &&
	git update-index --add world &&
	git commit -m second

'

test_expect_success 'branch switching' '

	git reset --hard &&
	test "$(git diff-files --raw)" = "" &&

	git checkout main &&
	test "$(git diff-files --raw)" = "" &&

	git checkout side &&
	test "$(git diff-files --raw)" = "" &&

	git checkout main &&
	test "$(git diff-files --raw)" = ""

'

test_expect_success 'path checkout' '

	git reset --hard &&
	test "$(git diff-files --raw)" = "" &&

	git checkout main world &&
	test "$(git diff-files --raw)" = "" &&

	git checkout side world &&
	test "$(git diff-files --raw)" = "" &&

	git checkout main world &&
	test "$(git diff-files --raw)" = ""

'

test_done

