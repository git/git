#!/bin/sh

test_description='cherry-pick should rerere for conflicts'

. ./test-lib.sh

test_expect_success setup '
	echo foo >foo &&
	git add foo && test_tick && git commit -q -m 1 &&
	echo foo-master >foo &&
	git add foo && test_tick && git commit -q -m 2 &&

	git checkout -b dev HEAD^ &&
	echo foo-dev >foo &&
	git add foo && test_tick && git commit -q -m 3 &&
	git config rerere.enabled true
'

test_expect_success 'conflicting merge' '
	test_must_fail git merge master
'

test_expect_success 'fixup' '
	echo foo-dev >foo &&
	git add foo && test_tick && git commit -q -m 4 &&
	git reset --hard HEAD^
	echo foo-dev >expect
'

test_expect_success 'cherry-pick conflict' '
	test_must_fail git cherry-pick master &&
	test_cmp expect foo
'

test_expect_success 'reconfigure' '
	git config rerere.enabled false
	git reset --hard
'

test_expect_success 'cherry-pick conflict without rerere' '
	test_must_fail git cherry-pick master &&
	test_must_fail test_cmp expect foo
'

test_done
