#!/bin/sh
#
# Copyright (c) 2006 Yann Dirson, based on t3400 by Amos Waterland
#

test_description='git cherry should detect patches integrated upstream

This test cherry-picks one local change of two into main branch, and
checks that git cherry only returns the second patch in the local branch
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_EMAIL=bogus_email_address
export GIT_AUTHOR_EMAIL

test_expect_success 'prepare repository with topic branch, and check cherry finds the 2 patches from there' '
	echo First > A &&
	git update-index --add A &&
	test_tick &&
	git commit -m "Add A." &&

	git checkout -b my-topic-branch &&

	echo Second > B &&
	git update-index --add B &&
	test_tick &&
	git commit -m "Add B." &&

	echo AnotherSecond > C &&
	git update-index --add C &&
	test_tick &&
	git commit -m "Add C." &&

	git checkout -f main &&
	rm -f B C &&

	echo Third >> A &&
	git update-index A &&
	test_tick &&
	git commit -m "Modify A." &&

	expr "$(echo $(git cherry main my-topic-branch) )" : "+ [^ ]* + .*"
'

test_expect_success 'check that cherry with limit returns only the top patch' '
	expr "$(echo $(git cherry main my-topic-branch my-topic-branch^1) )" : "+ [^ ]*"
'

test_expect_success 'cherry-pick one of the 2 patches, and check cherry recognized one and only one as new' '
	git cherry-pick my-topic-branch^0 &&
	echo $(git cherry main my-topic-branch) &&
	expr "$(echo $(git cherry main my-topic-branch) )" : "+ [^ ]* - .*"
'

test_expect_success 'cherry ignores whitespace' '
	git switch --orphan=upstream-with-space &&
	test_commit initial file &&
	>expect &&
	git switch --create=feature-without-space &&

	# A spaceless file on the feature branch.  Expect a match upstream.
	printf space >file &&
	git add file &&
	git commit -m"file without space" &&
	git log --format="- %H" -1 >>expect &&

	# A further change.  Should not match upstream.
	test_commit change file &&
	git log --format="+ %H" -1 >>expect &&

	git switch upstream-with-space &&
	# Same as the spaceless file, just with spaces and on upstream.
	test_commit "file with space" file "s p a c e" file-with-space &&
	git cherry upstream-with-space feature-without-space >actual &&
	test_cmp expect actual
'

test_done
