#!/bin/sh

test_description='--show-all --parents does not rewrite TREESAME commits'

. ./test-lib.sh

test_expect_success 'set up --show-all --parents test' '
	test_commit one foo.txt &&
	commit1=`git rev-list -1 HEAD` &&
	test_commit two bar.txt &&
	commit2=`git rev-list -1 HEAD` &&
	test_commit three foo.txt &&
	commit3=`git rev-list -1 HEAD`
	'

test_expect_success '--parents rewrites TREESAME parents correctly' '
	echo $commit3 $commit1 > expected &&
	echo $commit1 >> expected &&
	git rev-list --parents HEAD -- foo.txt > actual &&
	test_cmp expected actual
	'

test_expect_success '--parents --show-all does not rewrites TREESAME parents' '
	echo $commit3 $commit2 > expected &&
	echo $commit2 $commit1 >> expected &&
	echo $commit1 >> expected &&
	git rev-list --parents --show-all HEAD -- foo.txt > actual &&
	test_cmp expected actual
	'

test_done
