#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-merge hook.'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo Data for commit0. >a &&
	git update-index --add a &&
	tree0=$(git write-tree) &&
	commit0=$(echo setup | git commit-tree $tree0) &&
	echo Changed data for commit1. >a &&
	git update-index a &&
	tree1=$(git write-tree) &&
	commit1=$(echo modify | git commit-tree $tree1 -p $commit0) &&
	git update-ref refs/heads/main $commit0 &&
	git clone ./. clone1 &&
	GIT_DIR=clone1/.git git update-index --add a &&
	git clone ./. clone2 &&
	GIT_DIR=clone2/.git git update-index --add a
'

test_expect_success 'setup clone hooks' '
	test_when_finished "rm -f hook" &&
	cat >hook <<-\EOF &&
	echo $@ >>$GIT_DIR/post-merge.args
	EOF

	test_hook --setup -C clone1 post-merge <hook &&
	test_hook --setup -C clone2 post-merge <hook
'

test_expect_success 'post-merge does not run for up-to-date ' '
	GIT_DIR=clone1/.git git merge $commit0 &&
	! test -f clone1/.git/post-merge.args
'

test_expect_success 'post-merge runs as expected ' '
	GIT_DIR=clone1/.git git merge $commit1 &&
	test -e clone1/.git/post-merge.args
'

test_expect_success 'post-merge from normal merge receives the right argument ' '
	grep 0 clone1/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge runs as expected ' '
	GIT_DIR=clone2/.git git merge --squash $commit1 &&
	test -e clone2/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge receives the right argument ' '
	grep 1 clone2/.git/post-merge.args
'

test_done
