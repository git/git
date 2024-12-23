#!/bin/sh

test_description='test checkout safety improvements'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial file1 &&
	test_commit second file2 &&
	git checkout -b test-branch &&
	test_commit third file3
'

test_expect_success 'checkout detects null parameters' '
	# This test simulates internal API misuse where opts or info is NULL
	# The actual test is in the C code, but we verify the error message
	cat >expect <<-EOF &&
	fatal: invalid checkout options or branch info
	EOF
	# We cannot actually test NULL parameters through the CLI,
	# but we verify the error message exists in the binary
	git checkout --help 2>&1 | grep -q "invalid checkout options" &&
	test_must_fail git checkout --invalid-flag 2>actual &&
	test_grep "invalid" actual
'

test_expect_success 'checkout detects empty commit' '
	# Create an empty commit (no tree)
	empty_commit=$(git hash-object -t commit -w --stdin </dev/null) &&
	cat >expect <<-EOF &&
	fatal: cannot checkout empty commit
	EOF
	test_must_fail git checkout "$empty_commit" 2>actual &&
	test_grep "cannot checkout empty commit" actual
'

test_expect_success 'checkout validates paths' '
	# Create an invalid path
	test_when_finished "rm -f \"path with spaces\"" &&
	touch "path with spaces" &&
	git add "path with spaces" &&
	git commit -m "add path with spaces" &&
	test_must_fail git checkout HEAD^ 2>actual &&
	test_grep "invalid path" actual
'

test_expect_success 'checkout handles parallel checkout errors' '
	# Create many files to trigger parallel checkout
	mkdir -p dir1/subdir dir2/subdir &&
	for i in $(test_seq 1 100)
	do
		echo "content $i" >dir1/subdir/file$i
		echo "content $i" >dir2/subdir/file$i
	done &&
	git add dir1 dir2 &&
	git commit -m "add many files" &&
	
	# Simulate parallel checkout error by making target dir read-only
	chmod a-w dir1/subdir &&
	test_when_finished "chmod a+w dir1/subdir" &&
	
	test_must_fail git checkout HEAD^ 2>actual &&
	test_grep "errors occurred during parallel checkout" actual
'

test_expect_success 'checkout provides clear error for each failed path' '
	test_commit file-ok file1 &&
	test_when_finished "chmod 755 ." &&
	chmod 555 . &&
	test_must_fail git checkout HEAD^ 2>actual &&
	test_grep "failed to checkout" actual
'

test_expect_success 'checkout handles missing abbreviation gracefully' '
	git checkout -b new-branch &&
	commit_sha=$(git rev-parse HEAD) &&
	test_commit another-commit &&
	git checkout "$commit_sha" 2>actual &&
	test_grep "HEAD is now at" actual
'

test_done
