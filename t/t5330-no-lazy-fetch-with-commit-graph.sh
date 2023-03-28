#!/bin/sh

test_description='test for no lazy fetch with the commit-graph'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup: prepare a repository with a commit' '
	git init with-commit &&
	test_commit -C with-commit the-commit &&
	oid=$(git -C with-commit rev-parse HEAD)
'

test_expect_success 'setup: prepare a repository with commit-graph contains the commit' '
	git init with-commit-graph &&
	echo "$(pwd)/with-commit/.git/objects" \
		>with-commit-graph/.git/objects/info/alternates &&
	# create a ref that points to the commit in alternates
	git -C with-commit-graph update-ref refs/ref_to_the_commit "$oid" &&
	# prepare some other objects to commit-graph
	test_commit -C with-commit-graph something &&
	git -c gc.writeCommitGraph=true -C with-commit-graph gc &&
	test_path_is_file with-commit-graph/.git/objects/info/commit-graph
'

test_expect_success 'setup: change the alternates to what without the commit' '
	git init --bare without-commit &&
	git -C with-commit-graph cat-file -e $oid &&
	echo "$(pwd)/without-commit/objects" \
		>with-commit-graph/.git/objects/info/alternates &&
	test_must_fail git -C with-commit-graph cat-file -e $oid
'

test_expect_success 'fetch any commit from promisor with the usage of the commit graph' '
	# setup promisor and prepare any commit to fetch
	git -C with-commit-graph remote add origin "$(pwd)/with-commit" &&
	git -C with-commit-graph config remote.origin.promisor true &&
	git -C with-commit-graph config remote.origin.partialclonefilter blob:none &&
	test_commit -C with-commit any-commit &&
	anycommit=$(git -C with-commit rev-parse HEAD) &&
	GIT_TRACE="$(pwd)/trace.txt" \
		git -C with-commit-graph fetch origin $anycommit 2>err &&
	! grep "fatal: promisor-remote: unable to fork off fetch subprocess" err &&
	grep "git fetch origin" trace.txt >actual &&
	test_line_count = 1 actual
'

test_done
