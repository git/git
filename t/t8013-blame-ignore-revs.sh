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

	git blame --line-porcelain file >blame_raw &&

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse X >expect &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse X >expect &&
	test_cmp expect actual
	'

# Ignore X, make sure A is blamed for line 1 and B for line 2.
test_expect_success ignore_rev_changing_lines '
	git blame --line-porcelain --ignore-rev X file >blame_raw &&

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse A >expect &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual
	'

# For ignored revs that have added 'unblamable' lines, blame those lines on an
# all-zeros rev.
# 	A--B--X--Y
# Where Y changes lines 1 and 2, and adds lines 3 and 4.  The added lines ought
# to have nothing in common with "line-one" or "line-two", to keep any
# heuristics from matching them with any lines in the parent.
test_expect_success ignore_rev_adding_unblamable_lines '
	git config --add blame.maskIgnoredUnblamables true &&
	test_write_lines line-one-change line-two-changed y3 y4 >file &&
	git add file &&
	test_tick &&
	git commit -m Y &&
	git tag Y &&

	git rev-parse Y >y_rev &&
	sed -e "s/[0-9a-f]/0/g" y_rev >expect &&
	git blame --line-porcelain file --ignore-rev Y >blame_raw &&

	grep "^[0-9a-f]\+ [0-9]\+ 3" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 4" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual
	'

# Ignore X and Y, both in separate files.  Lines 1 == A, 2 == B.
test_expect_success ignore_revs_from_files '
	git rev-parse X >ignore_x &&
	git rev-parse Y >ignore_y &&
	git blame --line-porcelain file --ignore-revs-file ignore_x --ignore-revs-file ignore_y >blame_raw &&

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse A >expect &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual
	'

# Ignore X from the config option, Y from a file.
test_expect_success ignore_revs_from_configs_and_files '
	git config --add blame.ignoreRevsFile ignore_x &&
	git blame --line-porcelain file --ignore-revs-file ignore_y >blame_raw &&

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse A >expect &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual
	'

# Override blame.ignoreRevsFile (ignore_x) with an empty string.  X should be
# blamed now for lines 1 and 2, since we are no longer ignoring X.
test_expect_success override_ignore_revs_file '
	git blame --line-porcelain file --ignore-revs-file "" --ignore-revs-file ignore_y >blame_raw &&
	git rev-parse X >expect &&

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 2" blame_raw | sed -e "s/ .*//" >actual &&
	test_cmp expect actual
	'
test_expect_success bad_files_and_revs '
	test_must_fail git blame file --ignore-rev NOREV 2>err &&
	test_i18ngrep "Cannot find revision NOREV to ignore" err &&

	test_must_fail git blame file --ignore-revs-file NOFILE 2>err &&
	test_i18ngrep "Could not open object name list: NOFILE" err &&

	echo NOREV >ignore_norev &&
	test_must_fail git blame file --ignore-revs-file ignore_norev 2>err &&
	test_i18ngrep "Invalid object name: NOREV" err
	'

# Commit Z will touch the first two lines.  Y touched all four.
# 	A--B--X--Y--Z
# The blame output when ignoring Z should be:
# ^Y ... 1)
# ^Y ... 2)
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
	echo "*" >expect &&

	sed -n "1p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "2p" blame_raw | cut -c1 >actual &&
	test_cmp expect actual &&

	sed -n "3p" blame_raw | cut -c1 >actual &&
	! test_cmp expect actual &&

	sed -n "4p" blame_raw | cut -c1 >actual &&
	! test_cmp expect actual
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

	grep "^[0-9a-f]\+ [0-9]\+ 1" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse B >expect &&
	test_cmp expect actual &&

	grep "^[0-9a-f]\+ [0-9]\+ 9" blame_raw | sed -e "s/ .*//" >actual &&
	git rev-parse C >expect &&
	test_cmp expect actual
	'

test_done
