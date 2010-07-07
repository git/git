#!/bin/sh

test_description='merging with submodules'

. ./test-lib.sh

#
# history
#
#        a --- c
#      /   \ /
# root      X
#      \   / \
#        b --- d
#

test_expect_success setup '

	mkdir sub &&
	(cd sub &&
	 git init &&
	 echo original > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-root) &&
	git add sub &&
	test_tick &&
	git commit -m root &&

	git checkout -b a master &&
	(cd sub &&
	 echo A > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-a) &&
	git add sub &&
	test_tick &&
	git commit -m a &&

	git checkout -b b master &&
	(cd sub &&
	 echo B > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-b) &&
	git add sub &&
	test_tick &&
	git commit -m b &&

	git checkout -b c a &&
	git merge -s ours b &&

	git checkout -b d b &&
	git merge -s ours a
'

test_expect_success 'merging with modify/modify conflict' '

	git checkout -b test1 a &&
	test_must_fail git merge b &&
	test -f .git/MERGE_MSG &&
	git diff &&
	test -n "$(git ls-files -u)"
'

test_expect_success 'merging with a modify/modify conflict between merge bases' '

	git reset --hard HEAD &&
	git checkout -b test2 c &&
	git merge d

'

test_done
