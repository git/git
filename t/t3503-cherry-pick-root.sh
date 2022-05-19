#!/bin/sh

test_description='test cherry-picking (and reverting) a root cummit'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	echo first > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m "first" &&

	but symbolic-ref HEAD refs/heads/second &&
	rm .but/index file1 &&
	echo second > file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m "second" &&

	but symbolic-ref HEAD refs/heads/third &&
	rm .but/index file2 &&
	echo third > file3 &&
	but add file3 &&
	test_tick &&
	but cummit -m "third"

'

test_expect_success 'cherry-pick a root cummit' '

	but checkout second^0 &&
	but cherry-pick main &&
	echo first >expect &&
	test_cmp expect file1

'

test_expect_success 'revert a root cummit' '

	but revert main &&
	test_path_is_missing file1

'

test_expect_success 'cherry-pick a root cummit with an external strategy' '

	but cherry-pick --strategy=resolve main &&
	echo first >expect &&
	test_cmp expect file1

'

test_expect_success 'revert a root cummit with an external strategy' '

	but revert --strategy=resolve main &&
	test_path_is_missing file1

'

test_expect_success 'cherry-pick two root cummits' '

	echo first >expect.file1 &&
	echo second >expect.file2 &&
	echo third >expect.file3 &&

	but checkout second^0 &&
	but cherry-pick main third &&

	test_cmp expect.file1 file1 &&
	test_cmp expect.file2 file2 &&
	test_cmp expect.file3 file3 &&
	but rev-parse --verify HEAD^^ &&
	test_must_fail but rev-parse --verify HEAD^^^

'

test_done
