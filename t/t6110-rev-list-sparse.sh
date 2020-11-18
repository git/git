#!/bin/sh

test_description='operations that cull histories in unusual ways'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_commit A &&
	test_commit B &&
	test_commit C &&
	git checkout -b side HEAD^ &&
	test_commit D &&
	test_commit E &&
	git merge master
'

test_expect_success 'rev-list --first-parent --boundary' '
	git rev-list --first-parent --boundary HEAD^..
'

test_done
