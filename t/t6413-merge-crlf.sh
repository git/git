#!/bin/sh

test_description='merge conflict in crlf repo

		b---M
	       /   /
	initial---a

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	but config core.autocrlf true &&
	echo foo | append_cr >file &&
	but add file &&
	but cummit -m "Initial" &&
	but tag initial &&
	but branch side &&
	echo line from a | append_cr >file &&
	but cummit -m "add line from a" file &&
	but tag a &&
	but checkout side &&
	echo line from b | append_cr >file &&
	but cummit -m "add line from b" file &&
	but tag b &&
	but checkout main
'

test_expect_success 'Check "ours" is CRLF' '
	but reset --hard initial &&
	but merge side -s ours &&
	cat file | remove_cr | append_cr >file.temp &&
	test_cmp file file.temp
'

test_expect_success 'Check that conflict file is CRLF' '
	but reset --hard a &&
	test_must_fail but merge side &&
	cat file | remove_cr | append_cr >file.temp &&
	test_cmp file file.temp
'

test_done
