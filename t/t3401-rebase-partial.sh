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

test_expect_success \
    'prepare repository with topic branch' \
    'echo First > A &&
     git-update-index --add A &&
     git-commit -m "Add A." &&

     git-checkout -b my-topic-branch &&

     echo Second > B &&
     git-update-index --add B &&
     git-commit -m "Add B." &&

     echo AnotherSecond > C &&
     git-update-index --add C &&
     git-commit -m "Add C." &&

     git-checkout -f master &&

     echo Third >> A &&
     git-update-index A &&
     git-commit -m "Modify A."
'

test_expect_success \
    'pick top patch from topic branch into master' \
    'git-cherry-pick my-topic-branch^0 &&
     git-checkout -f my-topic-branch &&
     git-branch master-merge master &&
     git-branch my-topic-branch-merge my-topic-branch
'

test_debug \
    'git-cherry master &&
     git-format-patch -k --stdout --full-index master >/dev/null &&
     gitk --all & sleep 1
'

test_expect_success \
    'rebase topic branch against new master and check git-am did not get halted' \
    'git-rebase master && test ! -d .dotest'

test_expect_success \
	'rebase --merge topic branch that was partially merged upstream' \
	'git-checkout -f my-topic-branch-merge &&
	 git-rebase --merge master-merge &&
	 test ! -d .git/.dotest-merge'

test_done
