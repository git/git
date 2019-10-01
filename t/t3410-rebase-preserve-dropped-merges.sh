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

if ! test_have_prereq REBASE_P; then
	skip_all='skipping git rebase -p tests, as asked for'
	test_done
fi

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
	test_commit A file1 &&
	test_commit B file1 1 &&
	test_commit C file2 &&
	test_commit D file1 2 &&
	test_commit E file3 &&
	git checkout A &&
	test_commit F file4 &&
	test_commit G file1 3 &&
	test_commit H file5 &&
	git checkout F &&
	test_commit I file6
'

# A - B - C - D - E
#   \             \ \
#     F - G - H -- L \        -->   L
#       \            |               \
#         I -- G2 -- J -- K           I -- K
# G2 = same changes as G
test_expect_success 'skip same-resolution merges with -p' '
	git checkout H &&
	test_must_fail git merge E &&
	test_commit L file1 23 &&
	git checkout I &&
	test_commit G2 file1 3 &&
	test_must_fail git merge E &&
	test_commit J file1 23 &&
	test_commit K file7 file7 &&
	git rebase -i -p L &&
	test $(git rev-parse HEAD^^) = $(git rev-parse L) &&
	test "23" = "$(cat file1)" &&
	test "I" = "$(cat file6)" &&
	test "file7" = "$(cat file7)"
'

# A - B - C - D - E
#   \             \ \
#     F - G - H -- L2 \        -->   L2
#       \             |                \
#         I -- G3 --- J2 -- K2           I -- G3 -- K2
# G2 = different changes as G
test_expect_success 'keep different-resolution merges with -p' '
	git checkout H &&
	test_must_fail git merge E &&
	test_commit L2 file1 23 &&
	git checkout I &&
	test_commit G3 file1 4 &&
	test_must_fail git merge E &&
	test_commit J2 file1 24 &&
	test_commit K2 file7 file7 &&
	test_must_fail git rebase -i -p L2 &&
	echo 234 > file1 &&
	git add file1 &&
	git rebase --continue &&
	test $(git rev-parse HEAD^^^) = $(git rev-parse L2) &&
	test "234" = "$(cat file1)" &&
	test "I" = "$(cat file6)" &&
	test "file7" = "$(cat file7)"
'

test_done
