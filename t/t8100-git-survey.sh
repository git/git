#!/bin/sh

test_description='git survey (deprecated shim over `git repo structure`)'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=0
export TEST_PASSES_SANITIZE_LEAK

. ./test-lib.sh

test_expect_success 'git survey -h shows the deprecated banner' '
	test_expect_code 0 git survey -h >usage &&
	test_grep "DEPRECATED!" usage
'

test_expect_success 'create a semi-interesting repo' '
	test_commit_bulk 10 &&
	git tag -a -m one one HEAD~5 &&
	git tag -a -m two two HEAD~3 &&
	git tag -a -m three three two &&
	git tag -a -m four four three &&
	git update-ref -d refs/tags/three &&
	git update-ref -d refs/tags/two
'

test_expect_success 'survey prints a deprecation warning' '
	git survey --all-refs >out 2>err &&
	test_grep "is deprecated" err
'

test_expect_success 'survey forwards to git repo structure' '
	git survey --all-refs >survey-out 2>survey-err &&
	git repo structure --top=10 >structure-out 2>structure-err &&
	test_cmp structure-out survey-out
'

test_expect_success 'survey --top is translated' '
	git survey --top=3 --all-refs >out &&
	git repo structure --top=3 >expected &&
	test_cmp expected out
'

test_expect_success 'survey --branches translates to a refs/heads/* filter' '
	git survey --branches >out &&
	git repo structure --top=10 \
		--ref-filter="refs/heads/*" >expected &&
	test_cmp expected out
'

test_done
