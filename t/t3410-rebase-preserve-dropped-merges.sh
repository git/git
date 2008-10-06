#!/bin/sh
#
# Copyright (c) 2008 Stephen Haberman
#

test_description='git rebase preserve merges

This test runs git rebase with preserve merges and ensures commits
dropped by the --cherry-pick flag have their childrens parents
rewritten.
'
. ./test-lib.sh

# set up two branches like this:
#
# A - B - C - D - E
#   \
#     F - G - H
#       \
#         I
#
# where B, D and G touch the same file.

test_expect_success 'setup' '
	: > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m A &&
	git tag A &&
	echo 1 > file1 &&
	test_tick &&
	git commit -m B file1 &&
	: > file2 &&
	git add file2 &&
	test_tick &&
	git commit -m C &&
	echo 2 > file1 &&
	test_tick &&
	git commit -m D file1 &&
	: > file3 &&
	git add file3 &&
	test_tick &&
	git commit -m E &&
	git tag E &&
	git checkout -b branch1 A &&
	: > file4 &&
	git add file4 &&
	test_tick &&
	git commit -m F &&
	git tag F &&
	echo 3 > file1 &&
	test_tick &&
	git commit -m G file1 &&
	git tag G &&
	: > file5 &&
	git add file5 &&
	test_tick &&
	git commit -m H &&
	git tag H &&
	git checkout -b branch2 F &&
	: > file6 &&
	git add file6 &&
	test_tick &&
	git commit -m I &&
	git tag I
'

# A - B - C - D - E
#   \             \ \
#     F - G - H -- L \        -->   L
#       \            |               \
#         I -- G2 -- J -- K           I -- K
# G2 = same changes as G
test_expect_success 'skip same-resolution merges with -p' '
	git checkout branch1 &&
	! git merge E &&
	echo 23 > file1 &&
	git add file1 &&
	git commit -m L &&
	git checkout branch2 &&
	echo 3 > file1 &&
	git commit -a -m G2 &&
	! git merge E &&
	echo 23 > file1 &&
	git add file1 &&
	git commit -m J &&
	echo file7 > file7 &&
	git add file7 &&
	git commit -m K &&
	GIT_EDITOR=: git rebase -i -p branch1 &&
	test $(git rev-parse branch2^^) = $(git rev-parse branch1) &&
	test "23" = "$(cat file1)" &&
	test "" = "$(cat file6)" &&
	test "file7" = "$(cat file7)" &&

	git checkout branch1 &&
	git reset --hard H &&
	git checkout branch2 &&
	git reset --hard I
'

# A - B - C - D - E
#   \             \ \
#     F - G - H -- L \        -->   L
#       \            |               \
#         I -- G2 -- J -- K           I -- G2 -- K
# G2 = different changes as G
test_expect_success 'keep different-resolution merges with -p' '
	git checkout branch1 &&
	! git merge E &&
	echo 23 > file1 &&
	git add file1 &&
	git commit -m L &&
	git checkout branch2 &&
	echo 4 > file1 &&
	git commit -a -m G2 &&
	! git merge E &&
	echo 24 > file1 &&
	git add file1 &&
	git commit -m J &&
	echo file7 > file7 &&
	git add file7 &&
	git commit -m K &&
	! GIT_EDITOR=: git rebase -i -p branch1 &&
	echo 234 > file1 &&
	git add file1 &&
	GIT_EDITOR=: git rebase --continue &&
	test $(git rev-parse branch2^^^) = $(git rev-parse branch1) &&
	test "234" = "$(cat file1)" &&
	test "" = "$(cat file6)" &&
	test "file7" = "$(cat file7)" &&

	git checkout branch1 &&
	git reset --hard H &&
	git checkout branch2 &&
	git reset --hard I
'

test_done
