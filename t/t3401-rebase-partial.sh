#!/bin/sh
#
# Copyright (c) 2006 Yann Dirson, based on t3400 by Amos Waterland
#

test_description='git rebase should detect patches integrated upstream

This test cherry-picks one local change of two into master branch, and
checks that git rebase succeeds with only the second patch in the
local branch.
'
. ./test-lib.sh

test_expect_success 'prepare repository with topic branch' '
	test_commit A &&
	git checkout -b my-topic-branch &&
	test_commit B &&
	test_commit C &&
	git checkout -f master &&
	test_commit A2 A.t
'

test_expect_success 'pick top patch from topic branch into master' '
	git cherry-pick C &&
	git checkout -f my-topic-branch
'

test_debug '
	git cherry master &&
	git format-patch -k --stdout --full-index master >/dev/null &&
	gitk --all & sleep 1
'

test_expect_success 'rebase topic branch against new master and check git am did not get halted' '
	git rebase master &&
	test_path_is_missing .git/rebase-apply
'

test_expect_success 'rebase --merge topic branch that was partially merged upstream' '
	git reset --hard C &&
	git rebase --merge master &&
	test_path_is_missing .git/rebase-merge
'

test_done
