#!/bin/sh

test_description='test cherry-picking a root commit'

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
	git commit -m "second" &&

	git symbolic-ref HEAD refs/heads/third &&
	rm .git/index file2 &&
	echo third > file3 &&
	git add file3 &&
	test_tick &&
	git commit -m "third"

'

test_expect_success 'cherry-pick a root commit' '

	git checkout second^0 &&
	git cherry-pick master &&
	test first = $(cat file1)

'

test_expect_success 'cherry-pick two root commits' '

	echo first >expect.file1 &&
	echo second >expect.file2 &&
	echo third >expect.file3 &&

	git checkout second^0 &&
	git cherry-pick master third &&

	test_cmp expect.file1 file1 &&
	test_cmp expect.file2 file2 &&
	test_cmp expect.file3 file3 &&
	git rev-parse --verify HEAD^^ &&
	test_must_fail git rev-parse --verify HEAD^^^

'

test_done
