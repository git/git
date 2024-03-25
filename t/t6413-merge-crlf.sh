#!/bin/sh

test_description='merge conflict in crlf repo

		b---M
	       /   /
	initial---a

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	git config core.autocrlf true &&
	echo foo | append_cr >file &&
	git add file &&
	git commit -m "Initial" &&
	git tag initial &&
	git branch side &&
	echo line from a | append_cr >file &&
	git commit -m "add line from a" file &&
	git tag a &&
	git checkout side &&
	echo line from b | append_cr >file &&
	git commit -m "add line from b" file &&
	git tag b &&
	git checkout main
'

test_expect_success 'Check "ours" is CRLF' '
	git reset --hard initial &&
	git merge side -s ours &&
	remove_cr <file | append_cr >file.temp &&
	test_cmp file file.temp
'

test_expect_success 'Check that conflict file is CRLF' '
	git reset --hard a &&
	test_must_fail git merge side &&
	remove_cr <file | append_cr >file.temp &&
	test_cmp file file.temp
'

test_done
