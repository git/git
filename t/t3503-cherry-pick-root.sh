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
	git commit -m "second"

'

test_expect_success 'cherry-pick a root commit' '

	git cherry-pick master &&
	test first = $(cat file1)

'

test_done
