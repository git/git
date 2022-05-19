#!/bin/sh

test_description='ignore revisions when blaming'
. ./test-lib.sh

# Creates:
# 	A--B--X
# A added line 1 and B added line 2.  X makes changes to those lines.  Sanity
# check that X is blamed for both lines.
test_expect_success setup '
	test_cummit A file line1 &&

	echo line2 >>file &&
	but add file &&
	test_tick &&
	but cummit -m B &&
	but tag B &&

	test_write_lines line-one line-two >file &&
	but add file &&
	test_tick &&
	but cummit -m X &&
	but tag X &&
	but tag -a -m "X (annotated)" XT &&

	but blame --line-porcelain file >blame_raw &&

	grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse X >expect &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse X >expect &&
	test_cmp expect actual
'

# Ensure bogus --ignore-rev requests are caught
test_expect_success 'validate --ignore-rev' '
	test_must_fail but blame --ignore-rev X^{tree} file
'

# Ensure bogus --ignore-revs-file requests are silently accepted
test_expect_success 'validate --ignore-revs-file' '
	but rev-parse X^{tree} >ignore_x &&
	but blame --ignore-revs-file ignore_x file
'

for I in X XT
do
	# Ignore X (or XT), make sure A is blamed for line 1 and B for line 2.
	# Giving X (i.e. cummit) and XT (i.e. annotated tag to cummit) should
	# produce the same result.
	test_expect_success "ignore_rev_changing_lines ($I)" '
		but blame --line-porcelain --ignore-rev $I file >blame_raw &&

		grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
		but rev-parse A >expect &&
		test_cmp expect actual &&

		grep -E "^[0-9a-f]+ [0-9]+ 2" blame_raw | sed -e "s/ .*//" >actual &&
		but rev-parse B >expect &&
		test_cmp expect actual
	'
done

# For ignored revs that have added 'unblamable' lines, attribute those to the
# ignored cummit.
# 	A--B--X--Y
# Where Y changes lines 1 and 2, and adds lines 3 and 4.  The added lines ought
# to have nothing in common with "line-one" or "line-two", to keep any
# heuristics from matching them with any lines in the parent.
test_expect_success ignore_rev_adding_unblamable_lines '
	test_write_lines line-one-change line-two-changed y3 y4 >file &&
	but add file &&
	test_tick &&
	but cummit -m Y &&
	but tag Y &&

	but rev-parse Y >expect &&
	but blame --line-porcelain file --ignore-rev Y >blame_raw &&

	grep -E "^[0-9a-f]+ [0-9]+ 3" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 4" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual
'

# Ignore X and Y, both in separate files.  Lines 1 == A, 2 == B.
test_expect_success ignore_revs_from_files '
	but rev-parse X >ignore_x &&
	but rev-parse Y >ignore_y &&
	but blame --line-porcelain file --ignore-revs-file ignore_x --ignore-revs-file ignore_y >blame_raw &&

	grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse A >expect &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse B >expect &&
	test_cmp expect actual
'

# Ignore X from the config option, Y from a file.
test_expect_success ignore_revs_from_configs_and_files '
	but config --add blame.ignoreRevsFile ignore_x &&
	but blame --line-porcelain file --ignore-revs-file ignore_y >blame_raw &&

	grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse A >expect &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse B >expect &&
	test_cmp expect actual
'

# Override blame.ignoreRevsFile (ignore_x) with an empty string.  X should be
# blamed now for lines 1 and 2, since we are no longer ignoring X.
test_expect_success override_ignore_revs_file '
	but blame --line-porcelain file --ignore-revs-file "" --ignore-revs-file ignore_y >blame_raw &&
	but rev-parse X >expect &&

	grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual
	'
test_expect_success bad_files_and_revs '
	test_must_fail but blame file --ignore-rev NOREV 2>err &&
	test_i18ngrep "cannot find revision NOREV to ignore" err &&

	test_must_fail but blame file --ignore-revs-file NOFILE 2>err &&
	test_i18ngrep "could not open.*: NOFILE" err &&

	echo NOREV >ignore_norev &&
	test_must_fail but blame file --ignore-revs-file ignore_norev 2>err &&
	test_i18ngrep "invalid object name: NOREV" err
'

# For ignored revs that have added 'unblamable' lines, mark those lines with a
# '*'
# 	A--B--X--Y
# Lines 3 and 4 are from Y and unblamable.  This was set up in
# ignore_rev_adding_unblamable_lines.
test_expect_success mark_unblamable_lines '
	but config --add blame.markUnblamableLines true &&

	but blame --ignore-rev Y file >blame_raw &&
	echo "*" >expect &&

	sed -n "3p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "4p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual
'

# cummit Z will touch the first two lines.  Y touched all four.
# 	A--B--X--Y--Z
# The blame output when ignoring Z should be:
# ?Y ... 1)
# ?Y ... 2)
# Y  ... 3)
# Y  ... 4)
# We're checking only the first character
test_expect_success mark_ignored_lines '
	but config --add blame.markIgnoredLines true &&

	test_write_lines line-one-Z line-two-Z y3 y4 >file &&
	but add file &&
	test_tick &&
	but cummit -m Z &&
	but tag Z &&

	but blame --ignore-rev Z file >blame_raw &&
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

# For ignored revs that added 'unblamable' lines and more recent cummits changed
# the blamable lines, mark the unblamable lines with a
# '*'
# 	A--B--X--Y--Z
# Lines 3 and 4 are from Y and unblamable, as set up in
# ignore_rev_adding_unblamable_lines.  Z changed lines 1 and 2.
test_expect_success mark_unblamable_lines_intermediate '
	but config --add blame.markUnblamableLines true &&

	but blame --ignore-rev Y file >blame_raw 2>stderr &&
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
# This happens when one cummit adds several lines (cummit B below).  A later
# cummit (C) changes one line in the middle of B's change.  cummit C gets blamed
# for its change, and that breaks up B's change into multiple blame entries.
# When processing B, one of the blame_entries is outside A's window (which was
# zero - it had no lines added on its side of the diff).
#
# A--B--C, ignore B to test the ignore heuristic's boundary checks.
test_expect_success ignored_chunk_negative_parent_size '
	rm -rf .but/ &&
	but init &&

	test_write_lines L1 L2 L7 L8 L9 >file &&
	but add file &&
	test_tick &&
	but cummit -m A &&
	but tag A &&

	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	but add file &&
	test_tick &&
	but cummit -m B &&
	but tag B &&

	test_write_lines L1 L2 L3 L4 xxx L6 L7 L8 L9 >file &&
	but add file &&
	test_tick &&
	but cummit -m C &&
	but tag C &&

	but blame file --ignore-rev B >blame_raw
'

# Resetting the repo and creating:
#
# A--B--M
#  \   /
#   C-+
#
# 'A' creates a file.  B changes line 1, and C changes line 9.  M merges.
test_expect_success ignore_merge '
	rm -rf .but/ &&
	but init &&

	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	but add file &&
	test_tick &&
	but cummit -m A &&
	but tag A &&

	test_write_lines BB L2 L3 L4 L5 L6 L7 L8 L9 >file &&
	but add file &&
	test_tick &&
	but cummit -m B &&
	but tag B &&

	but reset --hard A &&
	test_write_lines L1 L2 L3 L4 L5 L6 L7 L8 CC >file &&
	but add file &&
	test_tick &&
	but cummit -m C &&
	but tag C &&

	test_merge M B &&
	but blame --line-porcelain file --ignore-rev M >blame_raw &&

	grep -E "^[0-9a-f]+ [0-9]+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse B >expect &&
	test_cmp expect actual &&

	grep -E "^[0-9a-f]+ [0-9]+ 9" blame_raw | sed -e "s/ .*//" >actual &&
	but rev-parse C >expect &&
	test_cmp expect actual
'

test_done
