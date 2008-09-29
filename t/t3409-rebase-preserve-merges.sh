#!/bin/sh
#
# Copyright(C) 2008 Stephen Habermann & Andreas Ericsson
#
test_description='git rebase -p should preserve merges

Run "git rebase -p" and check that merges are properly carried along
'
. ./test-lib.sh

GIT_AUTHOR_EMAIL=bogus_email_address
export GIT_AUTHOR_EMAIL

#echo 'Setting up:
#
#A1--A2  <-- origin/master
# \   \
#  B1--M  <-- topic
#   \
#    B2  <-- origin/topic
#
#'

test_expect_success 'setup for merge-preserving rebase' \
	'echo First > A &&
	git add A &&
	git-commit -m "Add A1" &&
	git checkout -b topic &&
	echo Second > B &&
	git add B &&
	git-commit -m "Add B1" &&
	git checkout -f master &&
	echo Third >> A &&
	git-commit -a -m "Modify A2" &&

	git clone ./. clone1 &&
	cd clone1 &&
	git checkout -b topic origin/topic &&
	git merge origin/master &&
	cd ..

	git clone ./. clone2
	cd clone2 &&
	git checkout -b topic origin/topic &&
	git merge origin/master &&
	cd .. &&

	git checkout topic &&
	echo Fourth >> B &&
	git commit -a -m "Modify B2"
'

test_expect_success 'rebase -p fakes interactive rebase' '
	cd clone2 &&
	git fetch &&
	git rebase -p origin/topic &&
	test 1 = $(git rev-list --all --pretty=oneline | grep "Modify A" | wc -l) &&
	test 1 = $(git rev-list --all --pretty=oneline | grep "Merge commit" | wc -l)
'

test_done
