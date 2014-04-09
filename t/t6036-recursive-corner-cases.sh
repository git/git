#!/bin/sh

test_description='recursive merge corner cases involving criss-cross merges'

. ./test-lib.sh

get_clean_checkout () {
	git reset --hard &&
	git clean -fdqx &&
	git checkout "$1"
}

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
'

test_expect_success 'merge simple rename+criss-cross with no modifications' '
	git reset --hard &&
	git checkout L2^0 &&

	test_must_fail git merge -s recursive R2^0 &&

	test 2 = $(git ls-files -s | wc -l) &&
	test 2 = $(git ls-files -u | wc -l) &&
	test 2 = $(git ls-files -o | wc -l) &&

	test $(git rev-parse :2:three) = $(git rev-parse L2:three) &&
	test $(git rev-parse :3:three) = $(git rev-parse R2:three) &&

	test $(git rev-parse L2:three) = $(git hash-object three~HEAD) &&
	test $(git rev-parse R2:three) = $(git hash-object three~R2^0)
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
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	ten="0 1 2 3 4 5 6 7 8 9"
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
'

test_expect_success 'merge criss-cross + rename merges with basic modification' '
	git reset --hard &&
	git checkout L2^0 &&

	test_must_fail git merge -s recursive R2^0 &&

	test 2 = $(git ls-files -s | wc -l) &&
	test 2 = $(git ls-files -u | wc -l) &&
	test 2 = $(git ls-files -o | wc -l) &&

	test $(git rev-parse :2:three) = $(git rev-parse L2:three) &&
	test $(git rev-parse :3:three) = $(git rev-parse R2:three) &&

	test $(git rev-parse L2:three) = $(git hash-object three~HEAD) &&
	test $(git rev-parse R2:three) = $(git hash-object three~R2^0)
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
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

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
'

test_expect_success 'git detects differently handled merges conflict' '
	git reset --hard &&
	git checkout D^0 &&

	git merge -s recursive E^0 && {
		echo "BAD: should have conflicted"
		test "Incorrectly merged content" = "$(cat new_a)" &&
			echo "BAD: Silently accepted wrong content"
		return 1
	}

	test 3 = $(git ls-files -s | wc -l) &&
	test 3 = $(git ls-files -u | wc -l) &&
	test 0 = $(git ls-files -o | wc -l) &&

	test $(git rev-parse :2:new_a) = $(git rev-parse D:new_a) &&
	test $(git rev-parse :3:new_a) = $(git rev-parse E:new_a) &&

	git cat-file -p B:new_a >>merged &&
	git cat-file -p C:new_a >>merge-me &&
	>empty &&
	test_must_fail git merge-file \
		-L "Temporary merge branch 2" \
		-L "" \
		-L "Temporary merge branch 1" \
		merged empty merge-me &&
	test $(git rev-parse :1:new_a) = $(git hash-object merged)
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
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

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
'

test_expect_success 'git detects conflict merging criss-cross+modify/delete' '
	git checkout D^0 &&

	test_must_fail git merge -s recursive E^0 &&

	test 2 -eq $(git ls-files -s | wc -l) &&
	test 2 -eq $(git ls-files -u | wc -l) &&

	test $(git rev-parse :1:file) = $(git rev-parse master:file) &&
	test $(git rev-parse :2:file) = $(git rev-parse B:file)
'

test_expect_success 'git detects conflict merging criss-cross+modify/delete, reverse direction' '
	git reset --hard &&
	git checkout E^0 &&

	test_must_fail git merge -s recursive D^0 &&

	test 2 -eq $(git ls-files -s | wc -l) &&
	test 2 -eq $(git ls-files -u | wc -l) &&

	test $(git rev-parse :1:file) = $(git rev-parse master:file) &&
	test $(git rev-parse :3:file) = $(git rev-parse B:file)
'

#
# criss-cross + modify/modify with very contrived file contents:
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
#   Commit C: file with contents 'C\n'
#   Commit D: file with contents 'D\n'
#   Commit E: file with contents:
#      <<<<<<< Temporary merge branch 1
#      C
#      =======
#      B
#      >>>>>>> Temporary merge branch 2
#
# Now, when we merge commits D & E, does git detect the conflict?

test_expect_success 'setup differently handled merges of content conflict' '
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	echo A >file &&
	git add file &&
	test_tick &&
	git commit -m A &&

	git branch B &&
	git checkout -b C &&
	echo C >file &&
	git add file &&
	test_tick &&
	git commit -m C &&

	git checkout B &&
	echo B >file &&
	git add file &&
	test_tick &&
	git commit -m B &&

	git checkout B^0 &&
	test_must_fail git merge C &&
	echo D >file &&
	git add file &&
	test_tick &&
	git commit -m D &&
	git tag D &&

	git checkout C^0 &&
	test_must_fail git merge B &&
	cat <<EOF >file &&
<<<<<<< Temporary merge branch 1
C
=======
B
>>>>>>> Temporary merge branch 2
EOF
	git add file &&
	test_tick &&
	git commit -m E &&
	git tag E
'

test_expect_failure 'git detects conflict w/ criss-cross+contrived resolution' '
	git checkout D^0 &&

	test_must_fail git merge -s recursive E^0 &&

	test 3 -eq $(git ls-files -s | wc -l) &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse :2:file) = $(git rev-parse D:file) &&
	test $(git rev-parse :3:file) = $(git rev-parse E:file)
'

#
# criss-cross + d/f conflict via add/add:
#   Commit A: Neither file 'a' nor directory 'a/' exists.
#   Commit B: Introduce 'a'
#   Commit C: Introduce 'a/file'
#   Commit D: Merge B & C, keeping 'a' and deleting 'a/'
#
# Two different later cases:
#   Commit E1: Merge B & C, deleting 'a' but keeping 'a/file'
#   Commit E2: Merge B & C, deleting 'a' but keeping a slightly modified 'a/file'
#
#      B   D
#      o---o
#     / \ / \
#  A o   X   ? F
#     \ / \ /
#      o---o
#      C   E1 or E2
#
# Merging D & E1 requires we first create a virtual merge base X from
# merging A & B in memory.  Now, if X could keep both 'a' and 'a/file' in
# the index, then the merge of D & E1 could be resolved cleanly with both
# 'a' and 'a/file' removed.  Since git does not currently allow creating
# such a tree, the best we can do is have X contain both 'a~<unique>' and
# 'a/file' resulting in the merge of D and E1 having a rename/delete
# conflict for 'a'.  (Although this merge appears to be unsolvable with git
# currently, git could do a lot better than it currently does with these
# d/f conflicts, which is the purpose of this test.)
#
# Merge of D & E2 has similar issues for path 'a', but should always result
# in a modify/delete conflict for path 'a/file'.
#
# We run each merge in both directions, to check for directional issues
# with D/F conflict handling.
#

test_expect_success 'setup differently handled merges of directory/file conflict' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	>ignore-me &&
	git add ignore-me &&
	test_tick &&
	git commit -m A &&
	git tag A &&

	git branch B &&
	git checkout -b C &&
	mkdir a &&
	echo 10 >a/file &&
	git add a/file &&
	test_tick &&
	git commit -m C &&

	git checkout B &&
	echo 5 >a &&
	git add a &&
	test_tick &&
	git commit -m B &&

	git checkout B^0 &&
	test_must_fail git merge C &&
	git clean -f &&
	rm -rf a/ &&
	echo 5 >a &&
	git add a &&
	test_tick &&
	git commit -m D &&
	git tag D &&

	git checkout C^0 &&
	test_must_fail git merge B &&
	git clean -f &&
	git rm --cached a &&
	echo 10 >a/file &&
	git add a/file &&
	test_tick &&
	git commit -m E1 &&
	git tag E1 &&

	git checkout C^0 &&
	test_must_fail git merge B &&
	git clean -f &&
	git rm --cached a &&
	printf "10\n11\n" >a/file &&
	git add a/file &&
	test_tick &&
	git commit -m E2 &&
	git tag E2
'

test_expect_success 'merge of D & E1 fails but has appropriate contents' '
	get_clean_checkout D^0 &&

	test_must_fail git merge -s recursive E1^0 &&

	test 2 -eq $(git ls-files -s | wc -l) &&
	test 1 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse :0:ignore-me) = $(git rev-parse A:ignore-me) &&
	test $(git rev-parse :2:a) = $(git rev-parse B:a)
'

test_expect_success 'merge of E1 & D fails but has appropriate contents' '
	get_clean_checkout E1^0 &&

	test_must_fail git merge -s recursive D^0 &&

	test 2 -eq $(git ls-files -s | wc -l) &&
	test 1 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse :0:ignore-me) = $(git rev-parse A:ignore-me) &&
	test $(git rev-parse :3:a) = $(git rev-parse B:a)
'

test_expect_success 'merge of D & E2 fails but has appropriate contents' '
	get_clean_checkout D^0 &&

	test_must_fail git merge -s recursive E2^0 &&

	test 4 -eq $(git ls-files -s | wc -l) &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 1 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse :2:a) = $(git rev-parse B:a) &&
	test $(git rev-parse :3:a/file) = $(git rev-parse E2:a/file) &&
	test $(git rev-parse :1:a/file) = $(git rev-parse C:a/file) &&
	test $(git rev-parse :0:ignore-me) = $(git rev-parse A:ignore-me) &&

	test -f a~HEAD
'

test_expect_success 'merge of E2 & D fails but has appropriate contents' '
	get_clean_checkout E2^0 &&

	test_must_fail git merge -s recursive D^0 &&

	test 4 -eq $(git ls-files -s | wc -l) &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 1 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse :3:a) = $(git rev-parse B:a) &&
	test $(git rev-parse :2:a/file) = $(git rev-parse E2:a/file) &&
	test $(git rev-parse :1:a/file) = $(git rev-parse C:a/file)
	test $(git rev-parse :0:ignore-me) = $(git rev-parse A:ignore-me) &&

	test -f a~D^0
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
	git reset --hard &&
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

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
'

test_expect_success 'handle rename/rename(1to2)/modify followed by what looks like rename/rename(2to1)/modify' '
	git checkout D^0 &&

	git merge -s recursive E^0 &&

	test 1 -eq $(git ls-files -s | wc -l) &&
	test 0 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse HEAD:newname) = $(git rev-parse E:newname)
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

test_expect_success 'setup criss-cross + rename/rename/add + modify/modify' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

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
'

test_expect_failure 'detect rename/rename/add-source for virtual merge-base' '
	git checkout D^0 &&

	git merge -s recursive E^0 &&

	test 3 -eq $(git ls-files -s | wc -l) &&
	test 0 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse HEAD:b) = $(git rev-parse A:a) &&
	test $(git rev-parse HEAD:c) = $(git rev-parse A:a) &&
	test "$(cat a)" = "$(printf "1\n2\n3\n4\n5\n6\n7\n8\n")"
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
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

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
'

test_expect_success 'virtual merge base handles rename/rename(1to2)/add-dest' '
	git checkout D^0 &&

	git merge -s recursive E^0 &&

	test 2 -eq $(git ls-files -s | wc -l) &&
	test 0 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test $(git rev-parse HEAD:a) = $(git rev-parse A:a) &&
	test $(git rev-parse HEAD:c) = $(git rev-parse E:c)
'

test_done
