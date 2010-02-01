#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git rebase assorted tests

This test runs git rebase and checks that the author information is not lost
among other things.
'
. ./test-lib.sh

GIT_AUTHOR_EMAIL=bogus_email_address
export GIT_AUTHOR_EMAIL

test_expect_success \
    'prepare repository with topic branches' \
    'git config core.logAllRefUpdates true &&
     echo First > A &&
     git update-index --add A &&
     git commit -m "Add A." &&
     git checkout -b my-topic-branch &&
     echo Second > B &&
     git update-index --add B &&
     git commit -m "Add B." &&
     git checkout -f master &&
     echo Third >> A &&
     git update-index A &&
     git commit -m "Modify A." &&
     git checkout -b side my-topic-branch &&
     echo Side >> C &&
     git add C &&
     git commit -m "Add C" &&
     git checkout -b nonlinear my-topic-branch &&
     echo Edit >> B &&
     git add B &&
     git commit -m "Modify B" &&
     git merge side &&
     git checkout -b upstream-merged-nonlinear &&
     git merge master &&
     git checkout -f my-topic-branch &&
     git tag topic
'

test_expect_success 'rebase on dirty worktree' '
     echo dirty >> A &&
     test_must_fail git rebase master'

test_expect_success 'rebase on dirty cache' '
     git add A &&
     test_must_fail git rebase master'

test_expect_success 'rebase against master' '
     git reset --hard HEAD &&
     git rebase master'

test_expect_success 'rebase against master twice' '
     git rebase master >out &&
     grep "Current branch my-topic-branch is up to date" out
'

test_expect_success 'rebase against master twice with --force' '
     git rebase --force-rebase master >out &&
     grep "Current branch my-topic-branch is up to date, rebase forced" out
'

test_expect_success 'rebase against master twice from another branch' '
     git checkout my-topic-branch^ &&
     git rebase master my-topic-branch >out &&
     grep "Current branch my-topic-branch is up to date" out
'

test_expect_success 'rebase fast-forward to master' '
     git checkout my-topic-branch^ &&
     git rebase my-topic-branch >out &&
     grep "Fast-forwarded HEAD to my-topic-branch" out
'

test_expect_success \
    'the rebase operation should not have destroyed author information' \
    '! (git log | grep "Author:" | grep "<>")'

test_expect_success 'HEAD was detached during rebase' '
     test $(git rev-parse HEAD@{1}) != $(git rev-parse my-topic-branch@{1})
'

test_expect_success 'rebase after merge master' '
     git reset --hard topic &&
     git merge master &&
     git rebase master &&
     ! (git show | grep "^Merge:")
'

test_expect_success 'rebase of history with merges is linearized' '
     git checkout nonlinear &&
     test 4 = $(git rev-list master.. | wc -l) &&
     git rebase master &&
     test 3 = $(git rev-list master.. | wc -l)
'

test_expect_success \
    'rebase of history with merges after upstream merge is linearized' '
     git checkout upstream-merged-nonlinear &&
     test 5 = $(git rev-list master.. | wc -l) &&
     git rebase master &&
     test 3 = $(git rev-list master.. | wc -l)
'

test_expect_success 'rebase a single mode change' '
     git checkout master &&
     echo 1 > X &&
     git add X &&
     test_tick &&
     git commit -m prepare &&
     git checkout -b modechange HEAD^ &&
     echo 1 > X &&
     git add X &&
     test_chmod +x A &&
     test_tick &&
     git commit -m modechange &&
     GIT_TRACE=1 git rebase master
'

test_expect_success 'Show verbose error when HEAD could not be detached' '
     : > B &&
     test_must_fail git rebase topic 2> output.err > output.out &&
     grep "Untracked working tree file .B. would be overwritten" output.err
'

test_expect_success 'rebase -q is quiet' '
     rm B &&
     git checkout -b quiet topic &&
     git rebase -q master > output.out 2>&1 &&
     test ! -s output.out
'

test_expect_success 'Rebase a commit that sprinkles CRs in' '
	(
		echo "One"
		echo "TwoQ"
		echo "Three"
		echo "FQur"
		echo "Five"
	) | q_to_cr >CR &&
	git add CR &&
	test_tick &&
	git commit -a -m "A file with a line with CR" &&
	git tag file-with-cr &&
	git checkout HEAD^0 &&
	git rebase --onto HEAD^^ HEAD^ &&
	git diff --exit-code file-with-cr:CR HEAD:CR
'

test_done
