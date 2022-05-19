#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-merge hook.'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo Data for cummit0. >a &&
	git update-index --add a &&
	tree0=$(git write-tree) &&
	cummit0=$(echo setup | git cummit-tree $tree0) &&
	echo Changed data for cummit1. >a &&
	git update-index a &&
	tree1=$(git write-tree) &&
	cummit1=$(echo modify | git cummit-tree $tree1 -p $cummit0) &&
	git update-ref refs/heads/main $cummit0 &&
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
	GIT_DIR=clone1/.git git merge $cummit0 &&
	! test -f clone1/.git/post-merge.args
'

test_expect_success 'post-merge runs as expected ' '
	GIT_DIR=clone1/.git git merge $cummit1 &&
	test -e clone1/.git/post-merge.args
'

test_expect_success 'post-merge from normal merge receives the right argument ' '
	grep 0 clone1/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge runs as expected ' '
	GIT_DIR=clone2/.git git merge --squash $cummit1 &&
	test -e clone2/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge receives the right argument ' '
	grep 1 clone2/.git/post-merge.args
'

test_done
