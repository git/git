#!/bin/sh

test_description='git survey'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=0
export TEST_PASSES_SANITIZE_LEAK

. ./test-lib.sh

test_expect_success 'git survey -h shows experimental warning' '
	test_expect_code 129 git survey -h >usage &&
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

approximate_sizes() {
	# very simplistic approximate rounding
	sed -Ee "s/  *(1[0-9][0-9])( |$)/ ~0.1kB\2/g" \
	  -e "s/  *(4[6-9][0-9]|5[0-6][0-9])( |$)/ ~0.5kB\2/g" \
	  -e "s/  *(5[6-9][0-9]|6[0-6][0-9])( |$)/ ~0.6kB\2/g" \
	  -e "s/  *1(4[89][0-9]|5[0-8][0-9])( |$)/ ~1.5kB\2/g" \
	  -e "s/  *1(69[0-9]|7[0-9][0-9])( |$)/ ~1.7kB\2/g" \
	  -e "s/  *1(79[0-9]|8[0-9][0-9])( |$)/ ~1.8kB\2/g" \
	  -e "s/  *2(1[0-9][0-9]|20[0-1])( |$)/ ~2.1kB\2/g" \
	  -e "s/  *2(3[0-9][0-9]|4[0-1][0-9])( |$)/ ~2.3kB\2/g" \
	  -e "s/  *2(5[0-9][0-9]|6[0-1][0-9])( |$)/ ~2.5kB\2/g" \
	 "$@"
}

test_expect_success 'git survey (default)' '
	git survey --all-refs >out 2>err &&
	test_line_count = 0 err &&

	test_oid_cache <<-EOF &&
	commits_sizes sha1:~1.5kB | ~2.1kB
	commits_sizes sha256:~1.8kB | ~2.5kB
	trees_sizes sha1:~0.5kB | ~1.7kB
	trees_sizes sha256:~0.6kB | ~2.3kB
	blobs_sizes sha1:~0.1kB | ~0.1kB
	blobs_sizes sha256:~0.1kB | ~0.1kB
	tags_sizes sha1:~0.5kB | ~0.5kB
	tags_sizes sha256:~0.5kB | ~0.6kB
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
	    Commits |    10 | $(test_oid commits_sizes)
	      Trees |    10 | $(test_oid trees_sizes)
	      Blobs |    10 | $(test_oid blobs_sizes)
	       Tags |     4 | $(test_oid tags_sizes)
	EOF

	approximate_sizes out >out-edited &&
	lines=$(wc -l <expect) &&
	head -n "$lines" <out-edited >out-trimmed &&
	test_cmp expect out-trimmed &&

	for type in "DIRECTORIES" "FILES"
	do
		for metric in "COUNT" "DISK SIZE" "INFLATED SIZE"
		do
			grep "TOP $type BY $metric" out || return 1
		done || return 1
	done
'

test_done
