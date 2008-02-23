#!/bin/sh

test_description='git log'

. ./test-lib.sh

test_expect_success setup '

	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m initial &&

	echo ichi >one &&
	git add one &&
	test_tick &&
	git commit -m second &&

	mkdir a &&
	echo ni >a/two &&
	git add a/two &&
	test_tick &&
	git commit -m third &&

	echo san >a/three &&
	git add a/three &&
	test_tick &&
	git commit -m fourth &&

	git rm a/three &&
	test_tick &&
	git commit -m fifth

'

test_expect_success 'diff-filter=A' '

	actual=$(git log --pretty="format:%s" --diff-filter=A HEAD) &&
	expect=$(echo fourth ; echo third ; echo initial) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=M' '

	actual=$(git log --pretty="format:%s" --diff-filter=M HEAD) &&
	expect=$(echo second) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=D' '

	actual=$(git log --pretty="format:%s" --diff-filter=D HEAD) &&
	expect=$(echo fifth) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'



test_done