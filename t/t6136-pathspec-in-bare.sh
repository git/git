#!/bin/sh

test_description='diagnosing out-of-scope pathspec'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup a bare and non-bare repository' '
	test_commit file1 &&
	git clone --bare . bare
'

test_expect_success 'log and ls-files in a bare repository' '
	(
		cd bare &&
		test_must_fail git log -- .. >out 2>err &&
		test_must_be_empty out &&
		test_grep "outside repository" err &&

		test_must_fail git ls-files -- .. >out 2>err &&
		test_must_be_empty out &&
		test_grep "outside repository" err
	)
'

test_expect_success 'log and ls-files in .git directory' '
	(
		cd .git &&
		test_must_fail git log -- .. >out 2>err &&
		test_must_be_empty out &&
		test_grep "outside repository" err &&

		test_must_fail git ls-files -- .. >out 2>err &&
		test_must_be_empty out &&
		test_grep "outside repository" err
	)
'

test_done
