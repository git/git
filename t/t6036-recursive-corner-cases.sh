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

test_expect_success setup '
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

test_expect_success merge '
	git reset --hard &&
	git checkout L2^0 &&

	test_must_fail git merge -s recursive R2^0
'

test_done
