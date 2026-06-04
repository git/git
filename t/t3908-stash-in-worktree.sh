#!/bin/sh
#
# Copyright (c) 2019 Johannes E Schindelin
#

test_description='Test git stash in a worktree'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	git worktree add wt &&
	test_commit -C wt in-worktree
'

test_expect_success 'apply in subdirectory' '
	mkdir wt/subdir &&
	(
		cd wt/subdir &&
		echo modified >../initial.t &&
		git stash &&
		git stash apply >out
	) &&
	grep "\.\.\/initial\.t" wt/subdir/out
'

test_done
