#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='per path merge controlled by merge attribute'

. ./test-lib.sh

test_expect_success setup '

	for f in text binary union
	do
		echo Initial >$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Initial &&

	git branch side &&
	for f in text binary union
	do
		echo Master >>$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Master &&

	git checkout side &&
	for f in text binary union
	do
		echo Side >>$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Side

'

test_expect_success merge '

	{
		echo "binary -merge"
		echo "union merge=union"
	} >.gitattributes &&

	if git merge master
	then
		echo Gaah, should have conflicted
		false
	else
		echo Ok, conflicted.
	fi
'

test_expect_success 'check merge result in index' '

	git ls-files -u | grep binary &&
	git ls-files -u | grep text &&
	! (git ls-files -u | grep union)

'

test_expect_success 'check merge result in working tree' '

	git cat-file -p HEAD:binary >binary-orig &&
	grep "<<<<<<<" text &&
	cmp binary-orig binary &&
	! grep "<<<<<<<" union &&
	grep Master union &&
	grep Side union

'

test_done
