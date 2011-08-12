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

	test 5 = $(git ls-files -s | wc -l) &&
	test 3 = $(git ls-files -u | wc -l) &&
	test 0 = $(git ls-files -o | wc -l) &&

	test $(git rev-parse :0:one) = $(git rev-parse L2:one) &&
	test $(git rev-parse :0:two) = $(git rev-parse R2:two) &&
	test $(git rev-parse :2:three) = $(git rev-parse L2:three) &&
	test $(git rev-parse :3:three) = $(git rev-parse R2:three) &&

	cp two merged &&
	>empty &&
	test_must_fail git merge-file \
		-L "Temporary merge branch 2" \
		-L "" \
		-L "Temporary merge branch 1" \
		merged empty one &&
	test $(git rev-parse :1:three) = $(git hash-object merged)
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

	test 5 = $(git ls-files -s | wc -l) &&
	test 3 = $(git ls-files -u | wc -l) &&
	test 0 = $(git ls-files -o | wc -l) &&

	test $(git rev-parse :0:one) = $(git rev-parse L2:one) &&
	test $(git rev-parse :0:two) = $(git rev-parse R2:two) &&
	test $(git rev-parse :2:three) = $(git rev-parse L2:three) &&
	test $(git rev-parse :3:three) = $(git rev-parse R2:three) &&

	head -n 10 two >merged &&
	cp one merge-me &&
	>empty &&
	test_must_fail git merge-file \
		-L "Temporary merge branch 2" \
		-L "" \
		-L "Temporary merge branch 1" \
		merged empty merge-me &&
	test $(git rev-parse :1:three) = $(git hash-object merged)
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

test_done
