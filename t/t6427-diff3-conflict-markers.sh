#!/bin/sh

test_description='recursive merge diff3 style conflict markers'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Setup:
#          L1
#            \
#             ?
#            /
#          R1
#
# Where:
#   L1 and R1 both have a file named 'content' but have no common history
#

test_expect_success 'setup no merge base' '
	test_create_repo no_merge_base &&
	(
		cd no_merge_base &&

		but checkout -b L &&
		test_cummit A content A &&

		but checkout --orphan R &&
		test_cummit B content B
	)
'

test_expect_success 'check no merge base' '
	(
		cd no_merge_base &&

		but checkout L^0 &&

		test_must_fail but -c merge.conflictstyle=diff3 merge --allow-unrelated-histories -s recursive R^0 &&

		grep "|||||| empty tree" content
	)
'

# Setup:
#          L1
#         /  \
#     main    ?
#         \  /
#          R1
#
# Where:
#   L1 and R1 have modified the same file ('content') in conflicting ways
#

test_expect_success 'setup unique merge base' '
	test_create_repo unique_merge_base &&
	(
		cd unique_merge_base &&

		test_cummit base content "1
2
3
4
5
" &&

		but branch L &&
		but branch R &&

		but checkout L &&
		test_cummit L content "1
2
3
4
5
7" &&

		but checkout R &&
		but rm content &&
		test_cummit R renamed "1
2
3
4
5
six"
	)
'

test_expect_success 'check unique merge base' '
	(
		cd unique_merge_base &&

		but checkout L^0 &&
		MAIN=$(but rev-parse --short main) &&

		test_must_fail but -c merge.conflictstyle=diff3 merge -s recursive R^0 &&

		grep "|||||| $MAIN:content" renamed
	)
'

# Setup:
#          L1---L2--L3
#         /  \ /      \
#     main    X1       ?
#         \  / \      /
#          R1---R2--R3
#
# Where:
#   cummits L1 and R1 have modified the same file in non-conflicting ways
#   X1 is an auto-generated merge-base used when merging L1 and R1
#   cummits L2 and R2 are merges of R1 and L1 into L1 and R1, respectively
#   cummits L3 and R3 both modify 'content' in conflicting ways
#

test_expect_success 'setup multiple merge bases' '
	test_create_repo multiple_merge_bases &&
	(
		cd multiple_merge_bases &&

		test_cummit initial content "1
2
3
4
5" &&

		but branch L &&
		but branch R &&

		# Create L1
		but checkout L &&
		test_cummit L1 content "0
1
2
3
4
5" &&

		# Create R1
		but checkout R &&
		test_cummit R1 content "1
2
3
4
5
6" &&

		# Create L2
		but checkout L &&
		but merge R1 &&

		# Create R2
		but checkout R &&
		but merge L1 &&

		# Create L3
		but checkout L &&
		test_cummit L3 content "0
1
2
3
4
5
A" &&

		# Create R3
		but checkout R &&
		but rm content &&
		test_cummit R3 renamed "0
2
3
4
5
six"
	)
'

test_expect_success 'check multiple merge bases' '
	(
		cd multiple_merge_bases &&

		but checkout L^0 &&

		test_must_fail but -c merge.conflictstyle=diff3 merge -s recursive R^0 &&

		grep "|||||| merged common ancestors:content" renamed
	)
'

test_expect_success 'rebase --merge describes parent of cummit being picked' '
	test_create_repo rebase &&
	(
		cd rebase &&
		test_cummit base file &&
		test_cummit main file &&
		but checkout -b side HEAD^ &&
		test_cummit side file &&
		test_must_fail but -c merge.conflictstyle=diff3 rebase --merge main &&
		grep "||||||| parent of" file
	)
'

test_expect_success 'rebase --apply describes fake ancestor base' '
	(
		cd rebase &&
		but rebase --abort &&
		test_must_fail but -c merge.conflictstyle=diff3 rebase --apply main &&
		grep "||||||| constructed merge base" file
	)
'

test_setup_zdiff3 () {
	test_create_repo zdiff3 &&
	(
		cd zdiff3 &&

		test_write_lines 1 2 3 4 5 6 7 8 9 >basic &&
		test_write_lines 1 2 3 AA 4 5 BB 6 7 8 >middle-common &&
		test_write_lines 1 2 3 4 5 6 7 8 9 >interesting &&
		test_write_lines 1 2 3 4 5 6 7 8 9 >evil &&

		but add basic middle-common interesting evil &&
		but cummit -m base &&

		but branch left &&
		but branch right &&

		but checkout left &&
		test_write_lines 1 2 3 4 A B C D E 7 8 9 >basic &&
		test_write_lines 1 2 3 CC 4 5 DD 6 7 8 >middle-common &&
		test_write_lines 1 2 3 4 A B C D E F G H I J 7 8 9 >interesting &&
		test_write_lines 1 2 3 4 X A B C 7 8 9 >evil &&
		but add -u &&
		but cummit -m letters &&

		but checkout right &&
		test_write_lines 1 2 3 4 A X C Y E 7 8 9 >basic &&
		test_write_lines 1 2 3 EE 4 5 FF 6 7 8 >middle-common &&
		test_write_lines 1 2 3 4 A B C 5 6 G H I J 7 8 9 >interesting &&
		test_write_lines 1 2 3 4 Y A B C B C 7 8 9 >evil &&
		but add -u &&
		but cummit -m permuted
	)
}

test_expect_success 'check zdiff3 markers' '
	test_setup_zdiff3 &&
	(
		cd zdiff3 &&

		but checkout left^0 &&

		base=$(but rev-parse --short HEAD^1) &&
		test_must_fail but -c merge.conflictstyle=zdiff3 merge -s recursive right^0 &&

		test_write_lines 1 2 3 4 A \
				 "<<<<<<< HEAD" B C D \
				 "||||||| $base" 5 6 \
				 ======= X C Y \
				 ">>>>>>> right^0" \
				 E 7 8 9 \
				 >expect &&
		test_cmp expect basic &&

		test_write_lines 1 2 3 \
				 "<<<<<<< HEAD" CC \
				 "||||||| $base" AA \
				 ======= EE \
				 ">>>>>>> right^0" \
				 4 5 \
				 "<<<<<<< HEAD" DD \
				 "||||||| $base" BB \
				 ======= FF \
				 ">>>>>>> right^0" \
				 6 7 8 \
				 >expect &&
		test_cmp expect middle-common &&

		test_write_lines 1 2 3 4 A B C \
				 "<<<<<<< HEAD" D E F \
				 "||||||| $base" 5 6 \
				 ======= 5 6 \
				 ">>>>>>> right^0" \
				 G H I J 7 8 9 \
				 >expect &&
		test_cmp expect interesting &&

		# Not passing this one yet; the common "B C" lines is still
		# being left in the conflict blocks on the left and right
		# sides.
		test_write_lines 1 2 3 4 \
				 "<<<<<<< HEAD" X A \
				 "||||||| $base" 5 6 \
				 ======= Y A B C \
				 ">>>>>>> right^0" \
				 B C 7 8 9 \
				 >expect &&
		test_cmp expect evil
	)
'

test_done
