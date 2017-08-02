#!/bin/sh

test_description='cherry-pick should rerere for conflicts'

. ./test-lib.sh

test_expect_success setup '
	test_commit foo &&
	test_commit foo-master foo &&

	git checkout -b dev foo &&
	test_commit foo-dev foo &&
	git config rerere.enabled true
'

test_expect_success 'conflicting merge' '
	test_must_fail git merge master
'

test_expect_success 'fixup' '
	echo foo-resolved >foo &&
	git commit -am resolved &&
	cp foo expect &&
	git reset --hard HEAD^
'

test_expect_success 'cherry-pick conflict' '
	test_must_fail git cherry-pick master &&
	test_cmp expect foo
'

test_expect_success 'reconfigure' '
	git config rerere.enabled false &&
	git reset --hard
'

test_expect_success 'cherry-pick conflict without rerere' '
	test_must_fail git cherry-pick master &&
	test_must_fail test_cmp expect foo
'

test_done
