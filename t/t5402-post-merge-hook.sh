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
	but update-index --add a &&
	tree0=$(but write-tree) &&
	cummit0=$(echo setup | but cummit-tree $tree0) &&
	echo Changed data for cummit1. >a &&
	but update-index a &&
	tree1=$(but write-tree) &&
	cummit1=$(echo modify | but cummit-tree $tree1 -p $cummit0) &&
	but update-ref refs/heads/main $cummit0 &&
	but clone ./. clone1 &&
	GIT_DIR=clone1/.but but update-index --add a &&
	but clone ./. clone2 &&
	GIT_DIR=clone2/.but but update-index --add a
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
	GIT_DIR=clone1/.but but merge $cummit0 &&
	! test -f clone1/.but/post-merge.args
'

test_expect_success 'post-merge runs as expected ' '
	GIT_DIR=clone1/.but but merge $cummit1 &&
	test -e clone1/.but/post-merge.args
'

test_expect_success 'post-merge from normal merge receives the right argument ' '
	grep 0 clone1/.but/post-merge.args
'

test_expect_success 'post-merge from squash merge runs as expected ' '
	GIT_DIR=clone2/.but but merge --squash $cummit1 &&
	test -e clone2/.but/post-merge.args
'

test_expect_success 'post-merge from squash merge receives the right argument ' '
	grep 1 clone2/.but/post-merge.args
'

test_done
