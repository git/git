#!/bin/sh

test_description='recursive merge corner cases involving criss-cross merges'

. ./test-lib.sh

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
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >expect       \
			L2:three   R2:three \
			L2:three   R2:three &&
		git rev-parse   >actual     \
			:2:three   :3:three &&
		git hash-object >>actual    \
			three~HEAD three~R2^0 &&
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
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >expect       \
			L2:three   R2:three \
			L2:three   R2:three &&
		git rev-parse   >actual     \
			:2:three   :3:three &&
		git hash-object >>actual    \
			three~HEAD three~R2^0 &&
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
		echo "other content" >>new_a &&
		git add a new_a &&
		test_tick && git commit -m C &&

		git checkout B &&
		git mv a new_a &&
		test_tick && git commit -m B &&

		git checkout B^0 &&
		test_must_fail git merge C &&
		git clean -f &&
		test_tick && git commit -m D &&
		git tag D &&

		git checkout C^0 &&
		test_must_fail git merge B &&
		rm new_a~HEAD new_a &&
		printf "Incorrectly merged content" >>new_a &&
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

		git rev-parse >expect       \
			D:new_a  E:new_a &&
		git rev-parse   >actual     \
			:2:new_a :3:new_a &&
		test_cmp expect actual &&

		git cat-file -p B:new_a >ours &&
		git cat-file -p C:new_a >theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "Temporary merge branch 2" \
			-L "" \
			-L "Temporary merge branch 1" \
			ours empty theirs &&
		sed -e "s/^\([<=>]\)/\1\1\1/" ours >expect &&
		git cat-file -p :1:new_a >actual &&
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
			master:file  B:file &&
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
			master:file  B:file &&
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
# slightly funny for merging D1 and e$, where E4 is defined as:
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
		git clean -fd &&
		git rm -rf a/ &&
		git rm a &&
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
		git clean -fd &&
		git rm -rf a/ &&
		test_write_lines 1 2 3 4 5 6 7 8 >a &&
		git add a &&
		git commit -m E3 &&
		git tag E3 &&

		git checkout C^0 &&
		test_must_fail git merge B^0 &&
		git clean -fd &&
		git rm -rf a/ &&
		git rm a &&
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
	)
'

test_expect_success 'merge of E1 & D1 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout E1^0 &&

		test_must_fail git merge -s recursive D1^0 &&

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
	)
'

test_expect_success 'merge of D1 & E2 fails but has appropriate contents' '
	test_when_finished "git -C directory-file reset --hard" &&
	test_when_finished "git -C directory-file clean -fdqx" &&
	(
		cd directory-file &&

		git checkout D1^0 &&

		test_must_fail git merge -s recursive E2^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >expect    \
			B:a   E2:a/file  C:a/file   A:ignore-me &&
		git rev-parse   >actual   \
			:2:a  :3:a/file  :1:a/file  :0:ignore-me &&
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

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >expect    \
			B:a   E2:a/file  C:a/file   A:ignore-me &&
		git rev-parse   >actual   \
			:3:a  :2:a/file  :1:a/file  :0:ignore-me &&
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

test_expect_success 'merge of D1 & E4 notifies user a and a2 are related' '
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
			A:ignore-me  B:a   D1:a  E4:a2 &&
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

test_done
