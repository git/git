#!/bin/sh

test_description='git reset should work on unborn branch'
. ./test-lib.sh

test_expect_success 'setup' '
	echo a >a &&
	echo b >b
'

test_expect_success 'reset' '
	git add a b &&
	git reset &&
	test "$(git ls-files)" = ""
'

test_expect_success 'reset HEAD' '
	rm .git/index &&
	git add a b &&
	test_must_fail git reset HEAD
'

test_expect_success 'reset $file' '
	rm .git/index &&
	git add a b &&
	git reset a &&
	test "$(git ls-files)" = "b"
'

test_expect_success 'reset -p' '
	rm .git/index &&
	git add a &&
	echo y | git reset -p &&
	test "$(git ls-files)" = ""
'

test_expect_success 'reset --soft is a no-op' '
	rm .git/index &&
	git add a &&
	git reset --soft
	test "$(git ls-files)" = "a"
'

test_expect_success 'reset --hard' '
	rm .git/index &&
	git add a &&
	git reset --hard &&
	test "$(git ls-files)" = "" &&
	test_path_is_missing a
'

test_done
