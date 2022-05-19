#!/bin/sh

test_description='but-merge with case-changing rename on case-insensitive file system'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if ! test_have_prereq CASE_INSENSITIVE_FS
then
	skip_all='skipping case insensitive tests - case sensitive file system'
	test_done
fi

test_expect_success 'merge with case-changing rename' '
	test $(but config core.ignorecase) = true &&
	>TestCase &&
	but add TestCase &&
	but cummit -m "add TestCase" &&
	but tag baseline &&
	but checkout -b with-camel &&
	>foo &&
	but add foo &&
	but cummit -m "intervening cummit" &&
	but checkout main &&
	but rm TestCase &&
	>testcase &&
	but add testcase &&
	but cummit -m "rename to testcase" &&
	but checkout with-camel &&
	but merge main -m "merge" &&
	test_path_is_file testcase
'

test_expect_success 'merge with case-changing rename on both sides' '
	but checkout main &&
	but reset --hard baseline &&
	but branch -D with-camel &&
	but checkout -b with-camel &&
	but mv TestCase testcase &&
	but cummit -m "recase on branch" &&
	>foo &&
	but add foo &&
	but cummit -m "intervening cummit" &&
	but checkout main &&
	but rm TestCase &&
	>testcase &&
	but add testcase &&
	but cummit -m "rename to testcase" &&
	but checkout with-camel &&
	but merge main -m "merge" &&
	test_path_is_file testcase
'

test_done
