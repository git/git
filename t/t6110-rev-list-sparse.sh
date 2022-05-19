#!/bin/sh

test_description='operations that cull histories in unusual ways'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_cummit A &&
	test_cummit B &&
	test_cummit C &&
	git checkout -b side HEAD^ &&
	test_cummit D &&
	test_cummit E &&
	git merge main
'

test_expect_success 'rev-list --first-parent --boundary' '
	git rev-list --first-parent --boundary HEAD^..
'

test_done
