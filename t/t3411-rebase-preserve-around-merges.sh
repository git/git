#!/bin/sh
#
# Copyright (c) 2008 Stephen Haberman
#

test_description='git rebase preserve merges

This test runs git rebase with -p and tries to squash a commit from after
a merge to before the merge.
'
. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

# set up two branches like this:
#
# A1 - B1 - D1 - E1 - F1
#       \        /
#        -- C1 --

test_expect_success 'setup' '
	test_commit A1 &&
	test_commit B1 &&
	test_commit C1 &&
	git reset --hard B1 &&
	test_commit D1 &&
	test_merge E1 C1 &&
	test_commit F1
'

# Should result in:
#
# A1 - B1 - D2 - E2
#       \        /
#        -- C1 --
#
test_expect_success 'squash F1 into D1' '
	FAKE_LINES="1 squash 3 2" git rebase -i -p B1 &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse C1)" &&
	test "$(git rev-parse HEAD~2)" = "$(git rev-parse B1)" &&
	git tag E2
'

# Start with:
#
# A1 - B1 - D2 - E2
#  \
#   G1 ---- L1 ---- M1
#    \             /
#     H1 -- J1 -- K1
#      \         /
#        -- I1 --
#
# And rebase G1..M1 onto E2

test_expect_success 'rebase two levels of merge' '
	test_commit G1 &&
	test_commit H1 &&
	test_commit I1 &&
	git checkout -b branch3 H1 &&
	test_commit J1 &&
	test_merge K1 I1 &&
	git checkout -b branch2 G1 &&
	test_commit L1 &&
	test_merge M1 K1 &&
	GIT_EDITOR=: git rebase -i -p E2 &&
	test "$(git rev-parse HEAD~3)" = "$(git rev-parse E2)" &&
	test "$(git rev-parse HEAD~2)" = "$(git rev-parse HEAD^2^2~2)" &&
	test "$(git rev-parse HEAD^2^1^1)" = "$(git rev-parse HEAD^2^2^1)"
'

test_done
