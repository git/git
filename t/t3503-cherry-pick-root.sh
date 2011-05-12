#!/bin/sh

test_description='test cherry-picking (and reverting) a root commit'

. ./test-lib.sh

test_expect_success setup '

	echo first > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "first" &&

	git symbolic-ref HEAD refs/heads/second &&
	rm .git/index file1 &&
	echo second > file2 &&
	git add file2 &&
	test_tick &&
	git commit -m "second"

'

test_expect_success 'cherry-pick a root commit' '

	git cherry-pick master &&
	echo first >expect &&
	test_cmp expect file1

'

test_expect_success 'revert a root commit' '

	git revert master &&
	test_path_is_missing file1

'

test_expect_success 'cherry-pick a root commit with an external strategy' '

	git cherry-pick --strategy=resolve master &&
	echo first >expect &&
	test_cmp expect file1

'

test_expect_success 'revert a root commit with an external strategy' '

	git revert --strategy=resolve master &&
	test_path_is_missing file1

'

test_done
