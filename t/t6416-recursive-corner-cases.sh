#!/bin/sh

test_description='recursive merge corner cases involving criss-cross merges'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh

#
#  L1  L2
#   o---o
#  / \ / \
# o   X   ?
#  \ / \ /
#   o---o
#  R1  R2
#

test_expect_success 'setup basic criss-cross + rename with no modifications' '
	test_create_repo basic-rename &&
	(
		cd basic-rename &&

		ten="0 1 2 3 4 5 6 7 8 9" &&
		for i in $ten
		do
			echo line $i in a sample file
		done >one &&
		for i in $ten
		do
			echo line $i in another sample file
		done >two &&
		git add one two &&
		test_tick && git commit -m initial &&

		git branch L1 &&
		git checkout -b R1 &&
		git mv one three &&
		test_tick && git commit -m R1 &&

		git checkout L1 &&
		git mv two three &&
		test_tick && git commit -m L1 &&

		git checkout L1^0 &&
		test_tick && git merge -s ours R1 &&
		git tag L2 &&

		git checkout R1^0 &&
		test_tick && git merge -s ours L1 &&
		git tag R2
	)
'

test_expect_success 'merge simple rename+criss-cross with no modifications' '
	(
		cd basic-rename &&

		git reset --hard &&
		git checkout L2^0 &&

		test_must_fail git merge -s recursive R2^0 &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect       \
			L2:three   R2:three &&
		git rev-parse   >actual     \
			:2:three   :3:three &&
		test_cmp expect actual
	)
'

#
# Same as before, but modify L1 slightly:
#
#  L1m L2
#   o---o
#  / \ / \
# o   X   ?
#  \ / \ /
#   o---o
#  R1  R2
#

test_expect_success 'setup criss-cross + rename merges with basic modification' '
	test_create_repo rename-modify &&
	(
		cd rename-modify &&

		ten="0 1 2 3 4 5 6 7 8 9" &&
		for i in $ten
		do
			echo line $i in a sample file
		done >one &&
		for i in $ten
		do
			echo line $i in another sample file
		done >two &&
		git add one two &&
		test_tick && git commit -m initial &&

		git branch L1 &&
		git checkout -b R1 &&
		git mv one three &&
		echo more >>two &&
		git add two &&
		test_tick && git commit -m R1 &&

		git checkout L1 &&
		git mv two three &&
		test_tick && git commit -m L1 &&

		git checkout L1^0 &&
		test_tick && git merge -s ours R1 &&
		git tag L2 &&

		git checkout R1^0 &&
		test_tick && git merge -s ours L1 &&
		git tag R2
	)
'

test_expect_success 'merge criss-cross + rename merges with basic modification' '
	(
		cd rename-modify &&

		git checkout L2^0 &&

		test_must_fail git merge -s recursive R2^0 &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect       \
			L2:three   R2:three &&
		git rev-parse   >actual     \
			:2:three   :3:three &&
		test_cmp expect actual
	)
'

#
# For the next test, we start with three commits in two lines of development
# which setup a rename/add conflict:
#   Commit A: File 'a' exists
#   Commit B: Rename 'a' -> 'new_a'
#   Commit C: Modify 'a', create different 'new_a'
# Later, two different people merge and resolve differently:
#   Commit D: Merge B & C, ignoring separately created 'new_a'
#   Commit E: Merge B & C making use of some piece of secondary 'new_a'
# Finally, someone goes to merge D & E.  Does git detect the conflict?
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#

test_expect_success 'setup differently handled merges of rename/add conflict' '
	test_create_repo rename-add &&
	(
		cd rename-add &&

		printf "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n" >a &&
		git add a &&
		test_tick && git commit -m A &&

		git branch B &&
		git checkout -b C &&
		echo 10 >>a &&
		test_write_lines 0 1 2 3 4 5 6 7 foobar >new_a &&
		git add a new_a &&
		test_tick && git commit -m C &&

		git checkout B &&
		git mv a new_a &&
		test_tick && git commit -m B &&

		git checkout B^0 &&
		test_must_fail git merge C &&
		git show :2:new_a >new_a &&
		git add new_a &&
		test_tick && git commit -m D &&
		git tag D &&

		git checkout C^0 &&
		test_must_fail git merge B &&
		test_write_lines 0 1 2 3 4 5 6 7 bad_merge >new_a &&
		git add -u &&
		test_tick && git commit -m E &&
		git tag E
	)
'

test_expect_success 'git detects differently handled merges conflict' '
	(
		cd rename-add &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git cat-file -p C:new_a >ours &&
		git cat-file -p C:a >theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "Temporary merge branch 1" \
			-L "" \
			-L "Temporary merge branch 2" \
			ours empty theirs &&
		sed -e "s/^\([<=>]\)/\1\1\1/" ours >ours-tweaked &&
		git hash-object ours-tweaked >expect &&
		git rev-parse >>expect      \
				  D:new_a  E:new_a &&
		git rev-parse   >actual     \
			:1:new_a :2:new_a :3:new_a &&
		test_cmp expect actual &&

		# Test that the two-way merge in new_a is as expected
		git cat-file -p D:new_a >ours &&
		git cat-file -p E:new_a >theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "E^0" \
			ours empty theirs &&
		sed -e "s/^\([<=>]\)/\1\1\1/" ours >expect &&
		git hash-object new_a >actual &&
		git hash-object ours  >expect &&
		test_cmp expect actual
	)
'

# Repeat the above testcase with precisely the same setup, other than with
# the two merge bases having different orderings of commit timestamps so
# that they are reversed in the order they are provided to merge-recursive,
# so that we can improve code coverage.
test_expect_success 'git detects differently handled merges conflict, swapped' '
	(
		cd rename-add &&

		# Difference #1: Do cleanup from previous testrun
		git reset --hard &&
		git clean -fdqx &&

		# Difference #2: Change commit timestamps
		btime=$(git log --no-walk --date=raw --format=%cd B | awk "{print \$1}") &&
		ctime=$(git log --no-walk --date=raw --format=%cd C | awk "{print \$1}") &&
		newctime=$(($btime+1)) &&
		git fast-export --no-data --all | sed -e s/$ctime/$newctime/ | git fast-import --force --quiet &&
		# End of most differences; rest is copy-paste of last test,
		# other than swapping C:a and C:new_a due to order switch

		git checkout D^0 &&
		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git cat-file -p C:a >ours &&
		git cat-file -p C:new_a >theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "Temporary merge branch 1" \
			-L "" \
			-L "Temporary merge branch 2" \
			ours empty theirs &&
		sed -e "s/^\([<=>]\)/\1\1\1/" ours >ours-tweaked &&
		git hash-object ours-tweaked >expect &&
		git rev-parse >>expect      \
				  D:new_a  E:new_a &&
		git rev-parse   >actual     \
			:1:new_a :2:new_a :3:new_a &&
		test_cmp expect actual &&

		# Test that the two-way merge in new_a is as expected
		git cat-file -p D:new_a >ours &&
		git cat-file -p E:new_a >theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "E^0" \
			ours empty theirs &&
		sed -e "s/^\([<=>]\)/\1\1\1/" ours >expect &&
		git hash-object new_a >actual &&
		git hash-object ours  >expect &&
		test_cmp expect actual
	)
'

#
# criss-cross + modify/delete:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: file with contents 'A\n'
#   Commit B: file with contents 'B\n'
#   Commit C: file not present
#   Commit D: file with contents 'B\n'
#   Commit E: file not present
#
# Merging commits D & E should result in modify/delete conflict.

test_expect_success 'setup criss-cross + modify/delete resolved differently' '
	test_create_repo modify-delete &&
	(
		cd modify-delete &&

		echo A >file &&
		git add file &&
		test_tick &&
		git commit -m A &&

		git branch B &&
		git checkout -b C &&
		git rm file &&
		test_tick &&
		git commit -m C &&

		git checkout B &&
		echo B >file &&
		git add file &&
		test_tick &&
		git commit -m B &&

		git checkout B^0 &&
		test_must_fail git merge C &&
		echo B >file &&
		git add file &&
		test_tick &&
		git commit -m D &&
		git tag D &&

		git checkout C^0 &&
		test_must_fail git merge B &&
		git rm file &&
		test_tick &&
		git commit -m E &&
		git tag E
	)
'

test_expect_success 'git detects conflict merging criss-cross+modify/delete' '
	(
		cd modify-delete &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&

		git rev-parse >expect       \
			main:file    B:file &&
		git rev-parse   >actual      \
			:1:file      :2:file &&
		test_cmp expect actual
	)
'

test_expect_success 'git detects conflict merging criss-cross+modify/delete, reverse direction' '
	(
		cd modify-delete &&

		git reset --hard &&
		git checkout E^0 &&

		test_must_fail git merge -s recursive D^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&

		git rev-parse >expect       \
			main:file    B:file &&
		git rev-parse   >actual      \
			:1:file      :3:file &&
		test_cmp expect actual
	)
'

#      SORRY FOR THE SUPER LONG DESCRIPTION, BUT THIS NEXT ONE IS HAIRY
#
# criss-cross + d/f conflict via add/add:
#   Commit A: Neither file 'a' nor directory 'a/' exists.
#   Commit B: Introduce 'a'
#   Commit C: Introduce 'a/file'
#   Commit D1: Merge B & C, keeping 'a'    and deleting 'a/'
#   Commit E1: Merge B & C, deleting 'a' but keeping 'a/file'
#
#      B   D1 or D2
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E1 or E2 or E3
#
# I'll describe D2, E2, & E3 (which are alternatives for D1 & E1) more below...
#
# Merging D1 & E1 requires we first create a virtual merge base X from
# merging A & B in memory.  There are several possibilities for the merge-base:
#   1: Keep both 'a' and 'a/file' (assuming crazy filesystem allowing a tree
#      with a directory and file at same path): results in merge of D1 & E1
#      being clean with both files deleted.  Bad (no conflict detected).
#   2: Keep 'a' but not 'a/file': Merging D1 & E1 is clean and matches E1.  Bad.
#   3: Keep 'a/file' but not 'a': Merging D1 & E1 is clean and matches D1.  Bad.
#   4: Keep neither file: Merging D1 & E1 reports the D/F add/add conflict.
#
# So 4 sounds good for this case, but if we were to merge D1 & E3, where E3
# is defined as:
#   Commit E3: Merge B & C, keeping modified a, and deleting a/
# then we'd get an add/add conflict for 'a', which seems suboptimal.  A little
# creativity leads us to an alternate choice:
#   5: Keep 'a' as 'a~$UNIQUE' and a/file; results:
#        Merge D1 & E1: rename/delete conflict for 'a'; a/file silently deleted
#        Merge D1 & E3 is clean, as expected.
#
# So choice 5 at least provides some kind of conflict for the original case,
# and can merge cleanly as expected with D1 and E3.  It also made things just
# slightly funny for merging D1 and E4, where E4 is defined as:
#   Commit E4: Merge B & C, modifying 'a' and renaming to 'a2', and deleting 'a/'
# in this case, we'll get a rename/rename(1to2) conflict because a~$UNIQUE
# gets renamed to 'a' in D1 and to 'a2' in E4.  But that's better than having
# two files (both 'a' and 'a2') sitting around without the user being notified
# that we could detect they were related and need to be merged.  Also, choice
# 5 makes the handling of 'a/file' seem suboptimal.  What if we were to merge
# D2 and E4, where D2 is:
#   Commit D2: Merge B & C, renaming 'a'->'a2', keeping 'a/file'
# This would result in a clean merge with 'a2' having three-way merged
# contents (good), and deleting 'a/' (bad) -- it doesn't detect the
# conflict in how the different sides treated a/file differently.
# Continuing down the creative route:
#   6: Keep 'a' as 'a~$UNIQUE1' and keep 'a/' as 'a~$UNIQUE2/'; results:
#        Merge D1 & E1: rename/delete conflict for 'a' and each path under 'a/'.
#        Merge D1 & E3: clean, as expected.
#        Merge D1 & E4: rename/rename(1to2) conflict on 'a' vs 'a2'.
#        Merge D2 & E4: clean for 'a2', rename/delete for a/file
#
# Choice 6 could cause rename detection to take longer (providing more targets
# that need to be searched).  Also, the conflict message for each path under
# 'a/' might be annoying unless we can detect it at the directory level, print
# it once, and then suppress it for individual filepaths underneath.
#
#
# As of time of writing, git uses choice 5.  Directory rename detection and
# rename detection performance improvements might make choice 6 a desirable
# improvement.  But we can at least document where we fall short for now...
#
#
# Historically, this testcase also used:
#   Commit E2: Merge B & C, deleting 'a' but keeping slightly modified 'a/file'
# The merge of D1 & E2 is very similar to D1 & E1 -- it has similar issues for
# path 'a', but should always result in a modify/delete conflict for path
# 'a/file'.  These tests ran the two merges
#   D1 & E1
#   D1 & E2
# in both directions, to check for directional issues with D/F conflict
# handling. Later we added
#   D1 & E3
#   D1 & E4
#   D2 & E4
# for good measure, though we only ran those one way because we had pretty
# good confidence in merge-recursive's directional handling of D/F issues.
#
# Just to summarize all the intermediate merge commits:
#   Commit D1: Merge B & C, keeping a    and deleting a/
#   Commit D2: Merge B & C, renaming a->a2, keeping a/file
#   Commit E1: Merge B & C, deleting a but keeping a/file
#   Commit E2: Merge B & C, deleting a but keeping slightly modified a/file
#   Commit E3: Merge B & C, keeping modified a, and deleting a/
#   Commit E4: Merge B & C, modifying 'a' and renaming to 'a2', and deleting 'a/'
#

test_expect_success 'setup differently handled merges of directory/file conflict' '
	test_create_repo directory-file &&
	(
		cd directory-file &&

		>ignore-me &&
		git add ignore-me &&
		test_tick &&
		git commit -m A &&
		git tag A &&

		git branch B &&
		git checkout -b C &&
		mkdir a &&
		test_write_lines a b c d e f g >a/file &&
		git add a/file &&
		test_tick &&
		git commit -m C &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 6 7 >a &&
		git add a &&
		test_tick &&
		git commit -m B &&

		git checkout B^0 &&
		git merge -s ours -m D1 C^0 &&
		git tag D1 &&

		git checkout B^0 &&
		test_must_fail git merge C^0 &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git rm -rf a/ &&
			git rm a~HEAD
		else
			git clean -fd &&
			git rm -rf a/ &&
			git rm a
		fi &&
		git cat-file -p B:a >a2 &&
		git add a2 &&
		git commit -m D2 &&
		git tag D2 &&

		git checkout C^0 &&
		git merge -s ours -m E1 B^0 &&
		git tag E1 &&

		git checkout C^0 &&
		git merge -s ours -m E2 B^0 &&
		test_write_lines a b c d e f g h >a/file &&
		git add a/file &&
		git commit --amend -C HEAD &&
		git tag E2 &&

		git checkout C^0 &&
		test_must_fail git merge B^0 &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git rm a~B^0
		else
			git clean -fd
		fi &&
		git rm -rf a/ &&
		test_write_lines 1 2 3 4 5 6 7 8 >a &&
		git add a &&
		git commit -m E3 &&
		git tag E3 &&

		git checkout C^0 &&
		test_must_fail git merge B^0 &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git rm -rf a/ &&
			git rm a~B^0
		else
			git clean -fd &&
			git rm -rf a/ &&
			git rm a
		fi &&
		test_write_lines 1 2 3 4 5 6 7 8 >a2 &&
		git add a2 &&
		git commit -m E4 &&
		git tag E4
	)
'

test_expect_success 'merge of D1 & E1 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D1^0 &&

		test_must_fail git merge -s recursive E1^0 &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				A:ignore-me  B:a  D1:a &&
			git rev-parse   >actual   \
				:0:ignore-me :1:a :2:a &&
			test_cmp expect actual
		else
			git ls-files -s >out &&
			test_line_count = 2 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				A:ignore-me  B:a &&
			git rev-parse   >actual   \
				:0:ignore-me :2:a &&
			test_cmp expect actual
		fi
	)
'

test_expect_success 'merge of E1 & D1 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout E1^0 &&

		test_must_fail git merge -s recursive D1^0 &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				A:ignore-me  B:a  D1:a &&
			git rev-parse   >actual   \
				:0:ignore-me :1:a :3:a &&
			test_cmp expect actual
		else
			git ls-files -s >out &&
			test_line_count = 2 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				A:ignore-me  B:a &&
			git rev-parse   >actual   \
				:0:ignore-me :3:a &&
			test_cmp expect actual
		fi
	)
'

test_expect_success 'merge of D1 & E2 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D1^0 &&

		test_must_fail git merge -s recursive E2^0 &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 5 out &&
			git ls-files -u >out &&
			test_line_count = 4 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				B:a       D1:a      E2:a/file  C:a/file   A:ignore-me &&
			git rev-parse   >actual   \
				:1:a~HEAD :2:a~HEAD :3:a/file  :1:a/file  :0:ignore-me
		else
			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 3 out &&
			git ls-files -o >out &&
			test_line_count = 2 out &&

			git rev-parse >expect    \
				B:a    E2:a/file  C:a/file   A:ignore-me &&
			git rev-parse   >actual   \
				:2:a   :3:a/file  :1:a/file  :0:ignore-me
		fi &&
		test_cmp expect actual &&

		test_path_is_file a~HEAD
	)
'

test_expect_success 'merge of E2 & D1 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout E2^0 &&

		test_must_fail git merge -s recursive D1^0 &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 5 out &&
			git ls-files -u >out &&
			test_line_count = 4 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >expect    \
				B:a       D1:a      E2:a/file  C:a/file   A:ignore-me &&
			git rev-parse   >actual   \
				:1:a~D1^0 :3:a~D1^0 :2:a/file  :1:a/file  :0:ignore-me
		else
			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 3 out &&
			git ls-files -o >out &&
			test_line_count = 2 out &&

			git rev-parse >expect    \
				B:a   E2:a/file  C:a/file   A:ignore-me &&
			git rev-parse   >actual   \
				:3:a  :2:a/file  :1:a/file  :0:ignore-me
		fi &&
		test_cmp expect actual &&

		test_path_is_file a~D1^0
	)
'

test_expect_success 'merge of D1 & E3 succeeds' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D1^0 &&

		git merge -s recursive E3^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect    \
			A:ignore-me  E3:a &&
		git rev-parse   >actual   \
			:0:ignore-me :0:a &&
		test_cmp expect actual
	)
'

test_expect_merge_algorithm failure success 'merge of D1 & E4 puts merge of a and a2 in both a and a2' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D1^0 &&

		test_must_fail git merge -s recursive E4^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect                  \
			A:ignore-me  B:a   E4:a2  E4:a2 &&
		git rev-parse   >actual                \
			:0:ignore-me :1:a~Temporary\ merge\ branch\ 2  :2:a  :3:a2 &&
		test_cmp expect actual
	)
'

test_expect_failure 'merge of D2 & E4 merges a2s & reports conflict for a/file' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D2^0 &&

		test_must_fail git merge -s recursive E4^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect                 \
			A:ignore-me  E4:a2  D2:a/file &&
		git rev-parse   >actual               \
			:0:ignore-me :0:a2  :2:a/file &&
		test_cmp expect actual
	)
'

#
# criss-cross with rename/rename(1to2)/modify followed by
# rename/rename(2to1)/modify:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: new file: a
#   Commit B: rename a->b, modifying by adding a line
#   Commit C: rename a->c
#   Commit D: merge B&C, resolving conflict by keeping contents in newname
#   Commit E: merge B&C, resolving conflict similar to D but adding another line
#
# There is a conflict merging B & C, but one of filename not of file
# content.  Whoever created D and E chose specific resolutions for that
# conflict resolution.  Now, since: (1) there is no content conflict
# merging B & C, (2) D does not modify that merged content further, and (3)
# both D & E resolve the name conflict in the same way, the modification to
# newname in E should not cause any conflicts when it is merged with D.
# (Note that this can be accomplished by having the virtual merge base have
# the merged contents of b and c stored in a file named a, which seems like
# the most logical choice anyway.)
#
# Comment from Junio: I do not necessarily agree with the choice "a", but
# it feels sound to say "B and C do not agree what the final pathname
# should be, but we know this content was derived from the common A:a so we
# use one path whose name is arbitrary in the virtual merge base X between
# D and E" and then further let the rename detection to notice that that
# arbitrary path gets renamed between X-D to "newname" and X-E also to
# "newname" to resolve it as both sides renaming it to the same new
# name. It is akin to what we do at the content level, i.e. "B and C do not
# agree what the final contents should be, so we leave the conflict marker
# but that may cancel out at the final merge stage".

test_expect_success 'setup rename/rename(1to2)/modify followed by what looks like rename/rename(2to1)/modify' '
	test_create_repo rename-squared-squared &&
	(
		cd rename-squared-squared &&

		printf "1\n2\n3\n4\n5\n6\n" >a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		echo 7 >>b &&
		git add -u &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a c &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge --no-commit -s ours C^0 &&
		git mv b newname &&
		git commit -m "Merge commit C^0 into HEAD" &&
		git tag D &&

		git checkout -q C^0 &&
		git merge --no-commit -s ours B^0 &&
		git mv c newname &&
		printf "7\n8\n" >>newname &&
		git add -u &&
		git commit -m "Merge commit B^0 into HEAD" &&
		git tag E
	)
'

test_expect_success 'handle rename/rename(1to2)/modify followed by what looks like rename/rename(2to1)/modify' '
	(
		cd rename-squared-squared &&

		git checkout D^0 &&

		git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 1 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test $(git rev-parse HEAD:newname) = $(git rev-parse E:newname)
	)
'

#
# criss-cross with rename/rename(1to2)/add-source + resolvable modify/modify:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: new file: a
#   Commit B: rename a->b
#   Commit C: rename a->c, add different a
#   Commit D: merge B&C, keeping b&c and (new) a modified at beginning
#   Commit E: merge B&C, keeping b&c and (new) a modified at end
#
# Merging commits D & E should result in no conflict; doing so correctly
# requires getting the virtual merge base (from merging B&C) right, handling
# renaming carefully (both in the virtual merge base and later), and getting
# content merge handled.

test_expect_success 'setup criss-cross + rename/rename/add-source + modify/modify' '
	test_create_repo rename-rename-add-source &&
	(
		cd rename-rename-add-source &&

		printf "lots\nof\nwords\nand\ncontent\n" >a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a c &&
		printf "2\n3\n4\n5\n6\n7\n" >a &&
		git add a &&
		git commit -m C &&

		git checkout B^0 &&
		git merge --no-commit -s ours C^0 &&
		git checkout C -- a c &&
		mv a old_a &&
		echo 1 >a &&
		cat old_a >>a &&
		rm old_a &&
		git add -u &&
		git commit -m "Merge commit C^0 into HEAD" &&
		git tag D &&

		git checkout C^0 &&
		git merge --no-commit -s ours B^0 &&
		git checkout B -- b &&
		echo 8 >>a &&
		git add -u &&
		git commit -m "Merge commit B^0 into HEAD" &&
		git tag E
	)
'

test_expect_failure 'detect rename/rename/add-source for virtual merge-base' '
	(
		cd rename-rename-add-source &&

		git checkout D^0 &&

		git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		printf "1\n2\n3\n4\n5\n6\n7\n8\n" >correct &&
		git rev-parse >expect \
			A:a   A:a     \
			correct       &&
		git rev-parse   >actual  \
			:0:b  :0:c       &&
		git hash-object >>actual \
			a                &&
		test_cmp expect actual
	)
'

#
# criss-cross with rename/rename(1to2)/add-dest + simple modify:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: new file: a
#   Commit B: rename a->b, add c
#   Commit C: rename a->c
#   Commit D: merge B&C, keeping A:a and B:c
#   Commit E: merge B&C, keeping A:a and slightly modified c from B
#
# Merging commits D & E should result in no conflict.  The virtual merge
# base of B & C needs to not delete B:c for that to work, though...

test_expect_success 'setup criss-cross+rename/rename/add-dest + simple modify' '
	test_create_repo rename-rename-add-dest &&
	(
		cd rename-rename-add-dest &&

		>a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		printf "1\n2\n3\n4\n5\n6\n7\n" >c &&
		git add c &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a c &&
		git commit -m C &&

		git checkout B^0 &&
		git merge --no-commit -s ours C^0 &&
		git mv b a &&
		git commit -m "D is like B but renames b back to a" &&
		git tag D &&

		git checkout B^0 &&
		git merge --no-commit -s ours C^0 &&
		git mv b a &&
		echo 8 >>c &&
		git add c &&
		git commit -m "E like D but has mod in c" &&
		git tag E
	)
'

test_expect_success 'virtual merge base handles rename/rename(1to2)/add-dest' '
	(
		cd rename-rename-add-dest &&

		git checkout D^0 &&

		git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect \
			A:a   E:c     &&
		git rev-parse   >actual \
			:0:a  :0:c      &&
		test_cmp expect actual
	)
'

#
# criss-cross with modify/modify on a symlink:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: simple simlink fickle->lagoon
#   Commit B: redirect fickle->disneyland
#   Commit C: redirect fickle->home
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious modify/modify conflict for the symlink 'fickle'.  Can
# git detect it?

test_expect_success 'setup symlink modify/modify' '
	test_create_repo symlink-modify-modify &&
	(
		cd symlink-modify-modify &&

		test_ln_s_add lagoon fickle &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git rm fickle &&
		test_ln_s_add disneyland fickle &&
		git commit -m B &&

		git checkout -b C A &&
		git rm fickle &&
		test_ln_s_add home fickle &&
		git add fickle &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_merge_algorithm failure success 'check symlink modify/modify' '
	(
		cd symlink-modify-modify &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

#
# criss-cross with add/add of a symlink:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: No symlink or path exists yet
#   Commit B: set up symlink: fickle->disneyland
#   Commit C: set up symlink: fickle->home
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious add/add conflict for the symlink 'fickle'.  Can
# git detect it?

test_expect_success 'setup symlink add/add' '
	test_create_repo symlink-add-add &&
	(
		cd symlink-add-add &&

		touch ignoreme &&
		git add ignoreme &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		test_ln_s_add disneyland fickle &&
		git commit -m B &&

		git checkout -b C A &&
		test_ln_s_add home fickle &&
		git add fickle &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_merge_algorithm failure success 'check symlink add/add' '
	(
		cd symlink-add-add &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

#
# criss-cross with modify/modify on a submodule:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: simple submodule repo
#   Commit B: update repo
#   Commit C: update repo differently
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious modify/modify conflict for the submodule 'repo'.  Can
# git detect it?

test_expect_success 'setup submodule modify/modify' '
	test_create_repo submodule-modify-modify &&
	(
		cd submodule-modify-modify &&

		test_create_repo submod &&
		(
			cd submod &&
			touch file-A &&
			git add file-A &&
			git commit -m A &&
			git tag A &&

			git checkout -b B A &&
			touch file-B &&
			git add file-B &&
			git commit -m B &&
			git tag B &&

			git checkout -b C A &&
			touch file-C &&
			git add file-C &&
			git commit -m C &&
			git tag C
		) &&

		git -C submod reset --hard A &&
		git add submod &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git -C submod reset --hard B &&
		git add submod &&
		git commit -m B &&

		git checkout -b C A &&
		git -C submod reset --hard C &&
		git add submod &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_merge_algorithm failure success 'check submodule modify/modify' '
	(
		cd submodule-modify-modify &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

#
# criss-cross with add/add on a submodule:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: nothing of note
#   Commit B: introduce submodule repo
#   Commit C: introduce submodule repo at different commit
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious add/add conflict for the submodule 'repo'.  Can
# git detect it?

test_expect_success 'setup submodule add/add' '
	test_create_repo submodule-add-add &&
	(
		cd submodule-add-add &&

		test_create_repo submod &&
		(
			cd submod &&
			touch file-A &&
			git add file-A &&
			git commit -m A &&
			git tag A &&

			git checkout -b B A &&
			touch file-B &&
			git add file-B &&
			git commit -m B &&
			git tag B &&

			git checkout -b C A &&
			touch file-C &&
			git add file-C &&
			git commit -m C &&
			git tag C
		) &&

		touch irrelevant-file &&
		git add irrelevant-file &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git -C submod reset --hard B &&
		git add submod &&
		git commit -m B &&

		git checkout -b C A &&
		git -C submod reset --hard C &&
		git add submod &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_merge_algorithm failure success 'check submodule add/add' '
	(
		cd submodule-add-add &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

#
# criss-cross with conflicting entry types:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: nothing of note
#   Commit B: introduce submodule 'path'
#   Commit C: introduce symlink 'path'
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious add/add conflict for 'path'.  Can git detect it?

test_expect_success 'setup conflicting entry types (submodule vs symlink)' '
	test_create_repo submodule-symlink-add-add &&
	(
		cd submodule-symlink-add-add &&

		test_create_repo path &&
		(
			cd path &&
			touch file-B &&
			git add file-B &&
			git commit -m B &&
			git tag B
		) &&

		touch irrelevant-file &&
		git add irrelevant-file &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git -C path reset --hard B &&
		git add path &&
		git commit -m B &&

		git checkout -b C A &&
		rm -rf path/ &&
		test_ln_s_add irrelevant-file path &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_merge_algorithm failure success 'check conflicting entry types (submodule vs symlink)' '
	(
		cd submodule-symlink-add-add &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

#
# criss-cross with regular files that have conflicting modes:
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E
#
#   Commit A: nothing of note
#   Commit B: introduce file source_me.bash, not executable
#   Commit C: introduce file source_me.bash, executable
#   Commit D: merge B&C, resolving in favor of B
#   Commit E: merge B&C, resolving in favor of C
#
# This is an obvious add/add mode conflict.  Can git detect it?

test_expect_success 'setup conflicting modes for regular file' '
	test_create_repo regular-file-mode-conflict &&
	(
		cd regular-file-mode-conflict &&

		touch irrelevant-file &&
		git add irrelevant-file &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		echo "command_to_run" >source_me.bash &&
		git add source_me.bash &&
		git commit -m B &&

		git checkout -b C A &&
		echo "command_to_run" >source_me.bash &&
		git add source_me.bash &&
		test_chmod +x source_me.bash &&
		git commit -m C &&

		git checkout -q B^0 &&
		git merge -s ours -m D C^0 &&
		git tag D &&

		git checkout -q C^0 &&
		git merge -s ours -m E B^0 &&
		git tag E
	)
'

test_expect_failure 'check conflicting modes for regular file' '
	(
		cd regular-file-mode-conflict &&

		git checkout D^0 &&

		test_must_fail git merge -s recursive E^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out
	)
'

# Setup:
#          L1---L2
#         /  \ /  \
#     main    X    ?
#         \  / \  /
#          R1---R2
#
# Where:
#   main has two files, named 'b' and 'a'
#   branches L1 and R1 both modify each of the two files in conflicting ways
#
#   L2 is a merge of R1 into L1; more on it later.
#   R2 is a merge of L1 into R1; more on it later.
#
#   X is an auto-generated merge-base used when merging L2 and R2.
#   since X is a merge of L1 and R1, it has conflicting versions of each file
#
#   More about L2 and R2:
#     - both resolve the conflicts in 'b' and 'a' differently
#     - L2 renames 'b' to 'm'
#     - R2 renames 'a' to 'm'
#
#   In the end, in file 'm' we have four different conflicting files (from
#   two versions of 'b' and two of 'a').  In addition, if
#   merge.conflictstyle is diff3, then the base version also has
#   conflict markers of its own, leading to a total of three levels of
#   conflict markers.  This is a pretty weird corner case, but we just want
#   to ensure that we handle it as well as practical.

test_expect_success 'setup nested conflicts' '
	test_create_repo nested_conflicts &&
	(
		cd nested_conflicts &&

		# Create some related files now
		for i in $(test_seq 1 10)
		do
			echo Random base content line $i
		done >initial &&

		cp initial b_L1 &&
		cp initial b_R1 &&
		cp initial b_L2 &&
		cp initial b_R2 &&
		cp initial a_L1 &&
		cp initial a_R1 &&
		cp initial a_L2 &&
		cp initial a_R2 &&

		test_write_lines b b_L1 >>b_L1 &&
		test_write_lines b b_R1 >>b_R1 &&
		test_write_lines b b_L2 >>b_L2 &&
		test_write_lines b b_R2 >>b_R2 &&
		test_write_lines a a_L1 >>a_L1 &&
		test_write_lines a a_R1 >>a_R1 &&
		test_write_lines a a_L2 >>a_L2 &&
		test_write_lines a a_R2 >>a_R2 &&

		# Setup original commit (or merge-base), consisting of
		# files named "b" and "a"
		cp initial b &&
		cp initial a &&
		echo b >>b &&
		echo a >>a &&
		git add b a &&
		test_tick && git commit -m initial &&

		git branch L &&
		git branch R &&

		# Handle the left side
		git checkout L &&
		mv -f b_L1 b &&
		mv -f a_L1 a &&
		git add b a &&
		test_tick && git commit -m "version L1 of files" &&
		git tag L1 &&

		# Handle the right side
		git checkout R &&
		mv -f b_R1 b &&
		mv -f a_R1 a &&
		git add b a &&
		test_tick && git commit -m "version R1 of files" &&
		git tag R1 &&

		# Create first merge on left side
		git checkout L &&
		test_must_fail git merge R1 &&
		mv -f b_L2 b &&
		mv -f a_L2 a &&
		git add b a &&
		git mv b m &&
		test_tick && git commit -m "left merge, rename b->m" &&
		git tag L2 &&

		# Create first merge on right side
		git checkout R &&
		test_must_fail git merge L1 &&
		mv -f b_R2 b &&
		mv -f a_R2 a &&
		git add b a &&
		git mv a m &&
		test_tick && git commit -m "right merge, rename a->m" &&
		git tag R2
	)
'

test_expect_success 'check nested conflicts' '
	(
		cd nested_conflicts &&

		git clean -f &&
		MAIN=$(git rev-parse --short main) &&
		git checkout L2^0 &&

		# Merge must fail; there is a conflict
		test_must_fail git -c merge.conflictstyle=diff3 merge -s recursive R2^0 &&

		# Make sure the index has the right number of entries
		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		# Ensure we have the correct number of untracked files
		git ls-files -o >out &&
		test_line_count = 1 out &&

		# Create a and b from virtual merge base X
		git cat-file -p main:a >base &&
		git cat-file -p L1:a >ours &&
		git cat-file -p R1:a >theirs &&
		test_must_fail git merge-file --diff3 \
			-L "Temporary merge branch 1" \
			-L "$MAIN"  \
			-L "Temporary merge branch 2" \
			ours  \
			base  \
			theirs &&
		sed -e "s/^\([<|=>]\)/\1\1/" ours >vmb_a &&

		git cat-file -p main:b >base &&
		git cat-file -p L1:b >ours &&
		git cat-file -p R1:b >theirs &&
		test_must_fail git merge-file --diff3 \
			-L "Temporary merge branch 1" \
			-L "$MAIN"  \
			-L "Temporary merge branch 2" \
			ours  \
			base  \
			theirs &&
		sed -e "s/^\([<|=>]\)/\1\1/" ours >vmb_b &&

		# Compare :2:m to expected values
		git cat-file -p L2:m >ours &&
		git cat-file -p R2:b >theirs &&
		test_must_fail git merge-file --diff3  \
			-L "HEAD:m"                    \
			-L "merged common ancestors:b" \
			-L "R2^0:b"                    \
			ours                           \
			vmb_b                          \
			theirs                         &&
		sed -e "s/^\([<|=>]\)/\1\1/" ours >m_stage_2 &&
		git cat-file -p :2:m >actual &&
		test_cmp m_stage_2 actual &&

		# Compare :3:m to expected values
		git cat-file -p L2:a >ours &&
		git cat-file -p R2:m >theirs &&
		test_must_fail git merge-file --diff3  \
			-L "HEAD:a"                    \
			-L "merged common ancestors:a" \
			-L "R2^0:m"                    \
			ours                           \
			vmb_a                          \
			theirs                         &&
		sed -e "s/^\([<|=>]\)/\1\1/" ours >m_stage_3 &&
		git cat-file -p :3:m >actual &&
		test_cmp m_stage_3 actual &&

		# Compare m to expected contents
		>empty &&
		cp m_stage_2 expected_final_m &&
		test_must_fail git merge-file --diff3 \
			-L "HEAD"                     \
			-L "merged common ancestors"  \
			-L "R2^0"                     \
			expected_final_m              \
			empty                         \
			m_stage_3                     &&
		test_cmp expected_final_m m
	)
'

# Setup:
#          L1---L2---L3
#         /  \ /  \ /  \
#     main    X1   X2   ?
#         \  / \  / \  /
#          R1---R2---R3
#
# Where:
#   main has one file named 'content'
#   branches L1 and R1 both modify each of the two files in conflicting ways
#
#   L<n> (n>1) is a merge of R<n-1> into L<n-1>
#   R<n> (n>1) is a merge of L<n-1> into R<n-1>
#   L<n> and R<n> resolve the conflicts differently.
#
#   X<n> is an auto-generated merge-base used when merging L<n+1> and R<n+1>.
#   By construction, X1 has conflict markers due to conflicting versions.
#   X2, due to using merge.conflictstyle=3, has nested conflict markers.
#
#   So, merging R3 into L3 using merge.conflictstyle=3 should show the
#   nested conflict markers from X2 in the base version -- that means we
#   have three levels of conflict markers.  Can we distinguish all three?

test_expect_success 'setup virtual merge base with nested conflicts' '
	test_create_repo virtual_merge_base_has_nested_conflicts &&
	(
		cd virtual_merge_base_has_nested_conflicts &&

		# Create some related files now
		for i in $(test_seq 1 10)
		do
			echo Random base content line $i
		done >content &&

		# Setup original commit
		git add content &&
		test_tick && git commit -m initial &&

		git branch L &&
		git branch R &&

		# Create L1
		git checkout L &&
		echo left >>content &&
		git add content &&
		test_tick && git commit -m "version L1 of content" &&
		git tag L1 &&

		# Create R1
		git checkout R &&
		echo right >>content &&
		git add content &&
		test_tick && git commit -m "version R1 of content" &&
		git tag R1 &&

		# Create L2
		git checkout L &&
		test_must_fail git -c merge.conflictstyle=diff3 merge R1 &&
		git checkout L1 content &&
		test_tick && git commit -m "version L2 of content" &&
		git tag L2 &&

		# Create R2
		git checkout R &&
		test_must_fail git -c merge.conflictstyle=diff3 merge L1 &&
		git checkout R1 content &&
		test_tick && git commit -m "version R2 of content" &&
		git tag R2 &&

		# Create L3
		git checkout L &&
		test_must_fail git -c merge.conflictstyle=diff3 merge R2 &&
		git checkout L1 content &&
		test_tick && git commit -m "version L3 of content" &&
		git tag L3 &&

		# Create R3
		git checkout R &&
		test_must_fail git -c merge.conflictstyle=diff3 merge L2 &&
		git checkout R1 content &&
		test_tick && git commit -m "version R3 of content" &&
		git tag R3
	)
'

test_expect_success 'check virtual merge base with nested conflicts' '
	(
		cd virtual_merge_base_has_nested_conflicts &&

		MAIN=$(git rev-parse --short main) &&
		git checkout L3^0 &&

		# Merge must fail; there is a conflict
		test_must_fail git -c merge.conflictstyle=diff3 merge -s recursive R3^0 &&

		# Make sure the index has the right number of entries
		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		# Ensure we have the correct number of untracked files
		git ls-files -o >out &&
		test_line_count = 1 out &&

		# Compare :[23]:content to expected values
		git rev-parse L1:content R1:content >expect &&
		git rev-parse :2:content :3:content >actual &&
		test_cmp expect actual &&

		# Imitate X1 merge base, except without long enough conflict
		# markers because a subsequent sed will modify them.  Put
		# result into vmb.
		git cat-file -p main:content >base &&
		git cat-file -p L:content >left &&
		git cat-file -p R:content >right &&
		cp left merged-once &&
		test_must_fail git merge-file --diff3 \
			-L "Temporary merge branch 1" \
			-L "$MAIN"  \
			-L "Temporary merge branch 2" \
			merged-once \
			base        \
			right       &&
		sed -e "s/^\([<|=>]\)/\1\1\1/" merged-once >vmb &&

		# Imitate X2 merge base, overwriting vmb.  Note that we
		# extend both sets of conflict markers to make them longer
		# with the sed command.
		cp left merged-twice &&
		test_must_fail git merge-file --diff3 \
			-L "Temporary merge branch 1" \
			-L "merged common ancestors"  \
			-L "Temporary merge branch 2" \
			merged-twice \
			vmb          \
			right        &&
		sed -e "s/^\([<|=>]\)/\1\1\1/" merged-twice >vmb &&

		# Compare :1:content to expected value
		git cat-file -p :1:content >actual &&
		test_cmp vmb actual &&

		# Determine expected content in final outer merge, compare to
		# what the merge generated.
		cp -f left expect &&
		test_must_fail git merge-file --diff3                      \
			-L "HEAD"  -L "merged common ancestors"  -L "R3^0" \
			expect     vmb                           right     &&
		test_cmp expect content
	)
'

test_done
