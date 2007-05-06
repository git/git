#!/bin/sh

test_description='git blame corner cases'
. ./test-lib.sh

pick_fc='s/^[0-9a-f^]* *\([^ ]*\) *(\([^ ]*\) .*/\1-\2/'

test_expect_success setup '

	echo A A A A A >one &&
	echo B B B B B >two &&
	echo C C C C C >tres &&
	echo ABC >mouse &&
	git add one two tres mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Initial git commit -m Initial &&

	cat one >uno &&
	mv two dos &&
	cat one >>tres &&
	echo DEF >>mouse
	git add uno dos tres mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Second git commit -a -m Second &&

	echo GHIJK >>mouse &&
	git add mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Third git commit -m Third &&

	cat mouse >cow &&
	git add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fourth git commit -m Fourth &&

	{
		echo ABC
		echo DEF
		echo XXXX
		echo GHIJK
	} >cow &&
	git add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fifth git commit -m Fifth
'

test_expect_success 'straight copy without -C' '

	git blame uno | grep Second

'

test_expect_success 'straight move without -C' '

	git blame dos | grep Initial

'

test_expect_success 'straight copy with -C' '

	git blame -C1 uno | grep Second

'

test_expect_success 'straight move with -C' '

	git blame -C1 dos | grep Initial

'

test_expect_success 'straight copy with -C -C' '

	git blame -C -C1 uno | grep Initial

'

test_expect_success 'straight move with -C -C' '

	git blame -C -C1 dos | grep Initial

'

test_expect_success 'append without -C' '

	git blame -L2 tres | grep Second

'

test_expect_success 'append with -C' '

	git blame -L2 -C1 tres | grep Second

'

test_expect_success 'append with -C -C' '

	git blame -L2 -C -C1 tres | grep Second

'

test_expect_success 'append with -C -C -C' '

	git blame -L2 -C -C -C1 tres | grep Initial

'

test_expect_success 'blame wholesale copy' '

	git blame -f -C -C1 HEAD^ -- cow | sed -e "$pick_fc" >current &&
	{
		echo mouse-Initial
		echo mouse-Second
		echo mouse-Third
	} >expected &&
	diff -u expected current

'

test_expect_success 'blame wholesale copy and more' '

	git blame -f -C -C1 HEAD -- cow | sed -e "$pick_fc" >current &&
	{
		echo mouse-Initial
		echo mouse-Second
		echo cow-Fifth
		echo mouse-Third
	} >expected &&
	diff -u expected current

'

test_done
