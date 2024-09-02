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

test_expect_success 'git survey --progress' '
	GIT_PROGRESS_DELAY=0 git survey --all-refs --progress >out 2>err &&
	grep "Preparing object walk" err
'

test_expect_success 'git survey (default)' '
	git survey --all-refs >out 2>err &&
	test_line_count = 0 err &&

	test_oid_cache <<-EOF &&
	commits_size_on_disk sha1:     1523
	commits_size_on_disk sha256:     1811

	commits_size sha1:         2153
	commits_size sha256:         2609

	trees_size_on_disk sha1:      495
	trees_size_on_disk sha256:      635

	trees_size sha1:         1706
	trees_size sha256:         2366

	tags_size sha1:          528
	tags_size sha256:          624

	tags_size_on_disk sha1:      510
	tags_size_on_disk sha256:      569
	EOF

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

	TOTAL OBJECT SIZES BY TYPE
	===============================================
	Object Type | Count | Disk Size | Inflated Size
	------------+-------+-----------+--------------
	    Commits |    10 | $(test_oid commits_size_on_disk) | $(test_oid commits_size)
	      Trees |    10 | $(test_oid trees_size_on_disk) | $(test_oid trees_size)
	      Blobs |    10 |       191 |           101
	       Tags |     4 | $(test_oid tags_size_on_disk) | $(test_oid tags_size)
	EOF

	test_cmp expect out
'

test_done
