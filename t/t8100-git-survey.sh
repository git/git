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
	test_commit_bulk 10 &&
	git tag -a -m one one HEAD~5 &&
	git tag -a -m two two HEAD~3 &&
	git tag -a -m three three two &&
	git tag -a -m four four three &&
	git update-ref -d refs/tags/three &&
	git update-ref -d refs/tags/two
'

test_expect_success 'git survey (default)' '
	git survey --all-refs >out 2>err &&
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
	      Tags (all) |     2
	Tags (annotated) |     2

	REACHABLE OBJECT SUMMARY
	========================
	Object Type | Count
	------------+------
	       Tags |     4
	    Commits |    10
	      Trees |    10
	      Blobs |    10
	EOF

	test_cmp expect out
'

test_done
