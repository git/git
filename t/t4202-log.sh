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

	git mv one ichi &&
	test_tick &&
	git commit -m third &&

	cp ichi ein &&
	git add ein &&
	test_tick &&
	git commit -m fourth &&

	mkdir a &&
	echo ni >a/two &&
	git add a/two &&
	test_tick &&
	git commit -m fifth  &&

	git rm a/two &&
	test_tick &&
	git commit -m sixth

'

test_expect_success 'diff-filter=A' '

	actual=$(git log --pretty="format:%s" --diff-filter=A HEAD) &&
	expect=$(echo fifth ; echo fourth ; echo third ; echo initial) &&
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
	expect=$(echo sixth ; echo third) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=R' '

	actual=$(git log -M --pretty="format:%s" --diff-filter=R HEAD) &&
	expect=$(echo third) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=C' '

	actual=$(git log -C -C --pretty="format:%s" --diff-filter=C HEAD) &&
	expect=$(echo fourth) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'git log --follow' '

	actual=$(git log --follow --pretty="format:%s" ichi) &&
	expect=$(echo third ; echo second ; echo initial) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'setup case sensitivity tests' '
	echo case >one &&
	test_tick &&
	git add one
	git commit -a -m Second
'

test_expect_success 'log --grep' '
	echo second >expect &&
	git log -1 --pretty="tformat:%s" --grep=sec >actual &&
	test_cmp expect actual
'

test_expect_success 'log -i --grep' '
	echo Second >expect &&
	git log -1 --pretty="tformat:%s" -i --grep=sec >actual &&
	test_cmp expect actual
'

test_expect_success 'log --grep -i' '
	echo Second >expect &&
	git log -1 --pretty="tformat:%s" --grep=sec -i >actual &&
	test_cmp expect actual
'

test_done

