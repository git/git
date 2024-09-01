#!/bin/sh

test_description='git survey'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=0
export TEST_PASSES_SANITIZE_LEAK

. ./test-lib.sh

test_expect_success 'git survey -h shows experimental warning' '
	test_expect_code 129 git survey -h 2>usage &&
	grep "EXPERIMENTAL!" usage
'

test_expect_success 'create a semi-interesting repo' '
	test_commit_bulk 10
'

test_expect_success 'git survey (default)' '
	git survey >out 2>err &&
	test_line_count = 0 err &&

	tr , " " >expect <<-EOF &&
	GIT SURVEY for "$(pwd)"
	-----------------------------------------------------

	REFERENCES SUMMARY
	========================
	,       Ref Type | Count
	-----------------+------
	,       Branches |     1
	     Remote refs |     0
	      Tags (all) |     0
	Tags (annotated) |     0
	EOF

	test_cmp expect out
'

test_done
