#!/bin/sh

test_description='recursive merge diff3 style conflict markers'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

		git checkout -b L &&
		test_commit A content A &&

		git checkout --orphan R &&
		test_commit B content B
	)
'

test_expect_success 'check no merge base' '
	(
		cd no_merge_base &&

		git checkout L^0 &&

		test_must_fail git -c merge.conflictstyle=diff3 merge --allow-unrelated-histories -s recursive R^0 &&

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

		test_commit base content "1
2
3
4
5
" &&

		git branch L &&
		git branch R &&

		git checkout L &&
		test_commit L content "1
2
3
4
5
7" &&

		git checkout R &&
		git rm content &&
		test_commit R renamed "1
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

		git checkout L^0 &&
		MAIN=$(git rev-parse --short main) &&

		test_must_fail git -c merge.conflictstyle=diff3 merge -s recursive R^0 &&

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
#   commits L1 and R1 have modified the same file in non-conflicting ways
#   X1 is an auto-generated merge-base used when merging L1 and R1
#   commits L2 and R2 are merges of R1 and L1 into L1 and R1, respectively
#   commits L3 and R3 both modify 'content' in conflicting ways
#

test_expect_success 'setup multiple merge bases' '
	test_create_repo multiple_merge_bases &&
	(
		cd multiple_merge_bases &&

		test_commit initial content "1
2
3
4
5" &&

		git branch L &&
		git branch R &&

		# Create L1
		git checkout L &&
		test_commit L1 content "0
1
2
3
4
5" &&

		# Create R1
		git checkout R &&
		test_commit R1 content "1
2
3
4
5
6" &&

		# Create L2
		git checkout L &&
		git merge R1 &&

		# Create R2
		git checkout R &&
		git merge L1 &&

		# Create L3
		git checkout L &&
		test_commit L3 content "0
1
2
3
4
5
A" &&

		# Create R3
		git checkout R &&
		git rm content &&
		test_commit R3 renamed "0
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

		git checkout L^0 &&

		test_must_fail git -c merge.conflictstyle=diff3 merge -s recursive R^0 &&

		grep "|||||| merged common ancestors:content" renamed
	)
'

test_expect_success 'rebase --merge describes parent of commit being picked' '
	test_create_repo rebase &&
	(
		cd rebase &&
		test_commit base file &&
		test_commit main file &&
		git checkout -b side HEAD^ &&
		test_commit side file &&
		test_must_fail git -c merge.conflictstyle=diff3 rebase --merge main &&
		grep "||||||| parent of" file
	)
'

test_expect_success 'rebase --apply describes fake ancestor base' '
	(
		cd rebase &&
		git rebase --abort &&
		test_must_fail git -c merge.conflictstyle=diff3 rebase --apply main &&
		grep "||||||| constructed merge base" file
	)
'

test_done
