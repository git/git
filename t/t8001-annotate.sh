#!/bin/sh

test_description='git annotate'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh

PROG='git annotate'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'annotate old revision' '
	git annotate file main >actual &&
	awk "{ print \$3; }" <actual >authors &&
	test 2 = $(grep A <authors | wc -l) &&
	test 2 = $(grep B <authors | wc -l)
'

test_done
