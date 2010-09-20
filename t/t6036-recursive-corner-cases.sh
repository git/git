#!/bin/sh

test_description='recursive merge corner cases'

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

test_expect_failure 'merge criss-cross + rename merges with basic modification' '
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

test_done
