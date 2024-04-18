#!/bin/sh

test_description='ignore revisions when blaming'
. ./test-lib.sh

# Creates:
# 	A--B--X
# A added line 1 and B added line 2.  X makes changes to those lines.  Sanity
# check that X is blamed for both lines.
test_expect_success setup '
	test_commit A file line1 &&

	echo line2 >>file &&
	git add file &&
	test_tick &&
	git commit -m B &&
	git tag B &&

	test_write_lines line-one line-two >file &&
	git add file &&
	test_tick &&
	git commit -m X &&
	git tag X &&
	git tag -a -m "X (annotated)" XT &&

	git blame --line-porcelain file >blame_raw &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
	git rev-parse X >expect &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 2/s/ .*//p" blame_raw >actual &&
	git rev-parse X >expect &&
	test_cmp expect actual
'

# Ensure bogus --ignore-rev requests are caught
test_expect_success 'validate --ignore-rev' '
	test_must_fail git blame --ignore-rev X^{tree} file
'

# Ensure bogus --ignore-revs-file requests are silently accepted
test_expect_success 'validate --ignore-revs-file' '
	git rev-parse X^{tree} >ignore_x &&
	git blame --ignore-revs-file ignore_x file
'

for I in X XT
do
	# Ignore X (or XT), make sure A is blamed for line 1 and B for line 2.
	# Giving X (i.e. commit) and XT (i.e. annotated tag to commit) should
	# produce the same result.
	test_expect_success "ignore_rev_changing_lines ($I)" '
		git blame --line-porcelain --ignore-rev $I file >blame_raw &&

		sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
		git rev-parse A >expect &&
		test_cmp expect actual &&

		sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 2/s/ .*//p" blame_raw >actual &&
		git rev-parse B >expect &&
		test_cmp expect actual
	'
done

# For ignored revs that have added 'unblamable' lines, attribute those to the
# ignored commit.
# 	A--B--X--Y
# Where Y changes lines 1 and 2, and adds lines 3 and 4.  The added lines ought
# to have nothing in common with "line-one" or "line-two", to keep any
# heuristics from matching them with any lines in the parent.
test_expect_success ignore_rev_adding_unblamable_lines '
	test_write_lines line-one-change line-two-changed y3 y4 >file &&
	git add file &&
	test_tick &&
	git commit -m Y &&
	git tag Y &&

	git rev-parse Y >expect &&
	git blame --line-porcelain file --ignore-rev Y >blame_raw &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 3/s/ .*//p" blame_raw >actual &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 4/s/ .*//p" blame_raw >actual &&
	test_cmp expect actual
'

# Ignore X and Y, both in separate files.  Lines 1 == A, 2 == B.
test_expect_success ignore_revs_from_files '
	git rev-parse X >ignore_x &&
	git rev-parse Y >ignore_y &&
	git blame --line-porcelain file --ignore-revs-file ignore_x --ignore-revs-file ignore_y >blame_raw &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
	git rev-parse A >expect &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 2/s/ .*//p" blame_raw >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual
'

# Ignore X from the config option, Y from a file.
test_expect_success ignore_revs_from_configs_and_files '
	git config --add blame.ignoreRevsFile ignore_x &&
	git blame --line-porcelain file --ignore-revs-file ignore_y >blame_raw &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
	git rev-parse A >expect &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 2/s/ .*//p" blame_raw >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual
'

# Override blame.ignoreRevsFile (ignore_x) with an empty string.  X should be
# blamed now for lines 1 and 2, since we are no longer ignoring X.
test_expect_success override_ignore_revs_file '
	git blame --line-porcelain file --ignore-revs-file "" --ignore-revs-file ignore_y >blame_raw &&
	git rev-parse X >expect &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 2/s/ .*//p" blame_raw >actual &&
	test_cmp expect actual
	'
test_expect_success bad_files_and_revs '
	test_must_fail git blame file --ignore-rev NOREV 2>err &&
	test_grep "cannot find revision NOREV to ignore" err &&

	test_must_fail git blame file --ignore-revs-file NOFILE 2>err &&
	test_grep "could not open.*: NOFILE" err &&

	echo NOREV >ignore_norev &&
	test_must_fail git blame file --ignore-revs-file ignore_norev 2>err &&
	test_grep "invalid object name: NOREV" err
'

# For ignored revs that have added 'unblamable' lines, mark those lines with a
# '*'
# 	A--B--X--Y
# Lines 3 and 4 are from Y and unblamable.  This was set up in
# ignore_rev_adding_unblamable_lines.
test_expect_success mark_unblamable_lines '
	git config --add blame.markUnblamableLines true &&

	git blame --ignore-rev Y file >blame_raw &&
	echo "*" >expect &&

	sed -n "3p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "4p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual
'

# Commit Z will touch the first two lines.  Y touched all four.
# 	A--B--X--Y--Z
# The blame output when ignoring Z should be:
# ?Y ... 1)
# ?Y ... 2)
# Y  ... 3)
# Y  ... 4)
# We're checking only the first character
test_expect_success mark_ignored_lines '
	git config --add blame.markIgnoredLines true &&

	test_write_lines line-one-Z line-two-Z y3 y4 >file &&
	git add file &&
	test_tick &&
	git commit -m Z &&
	git tag Z &&

	git blame --ignore-rev Z file >blame_raw &&
	echo "?" >expect &&

	sed -n "1p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "2p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "3p" blame_raw | cut -c1 >actual &&
	! test_cmp expect actual &&

	sed -n "4p" blame_raw | cut -c1 >actual &&
	! test_cmp expect actual
'

# For ignored revs that added 'unblamable' lines and more recent commits changed
# the blamable lines, mark the unblamable lines with a
# '*'
# 	A--B--X--Y--Z
# Lines 3 and 4 are from Y and unblamable, as set up in
# ignore_rev_adding_unblamable_lines.  Z changed lines 1 and 2.
test_expect_success mark_unblamable_lines_intermediate '
	git config --add blame.markUnblamableLines true &&

	git blame --ignore-rev Y file >blame_raw 2>stderr &&
	echo "*" >expect &&

	sed -n "3p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "4p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual
'

# The heuristic called by guess_line_blames() tries to find the size of a
# blame_entry 'e' in the parent's address space.  Those calculations need to
# check for negative or zero values for when a blame entry is completely outside
# the window of the parent's version of a file.
#
# This happens when one commit adds several lines (commit B below).  A later
# commit (C) changes one line in the middle of B's change.  Commit C gets blamed
# for its change, and that breaks up B's change into multiple blame entries.
# When processing B, one of the blame_entries is outside A's window (which was
# zero - it had no lines added on its side of the diff).
#
# A--B--C, ignore B to test the ignore heuristic's boundary checks.
test_expect_success ignored_chunk_negative_parent_size '
	rm -rf .git/ &&
	git init &&

	test_write_lines L1 L2 L7 L8 L9 >file &&
	git add file &&
	test_tick &&
	git commit -m A &&
	git tag A &&

	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	git add file &&
	test_tick &&
	git commit -m B &&
	git tag B &&

	test_write_lines L1 L2 L3 L4 xxx L6 L7 L8 L9 >file &&
	git add file &&
	test_tick &&
	git commit -m C &&
	git tag C &&

	git blame file --ignore-rev B >blame_raw
'

# Resetting the repo and creating:
#
# A--B--M
#  \   /
#   C-+
#
# 'A' creates a file.  B changes line 1, and C changes line 9.  M merges.
test_expect_success ignore_merge '
	rm -rf .git/ &&
	git init &&

	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	git add file &&
	test_tick &&
	git commit -m A &&
	git tag A &&

	test_write_lines BB L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	git add file &&
	test_tick &&
	git commit -m B &&
	git tag B &&

	git reset --hard A &&
	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 CC >file &&
	git add file &&
	test_tick &&
	git commit -m C &&
	git tag C &&

	test_merge M B &&
	git blame --line-porcelain file --ignore-rev M >blame_raw &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 1/s/ .*//p" blame_raw >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual &&

	sed -ne "/^[0-9a-f][0-9a-f]* [0-9][0-9]* 9/s/ .*//p" blame_raw >actual &&
	git rev-parse C >expect &&
	test_cmp expect actual
'

test_done
