#!/bin/sh

test_description='--all includes detached HEADs'

. ./test-lib.sh


cummit () {
	test_tick &&
	echo $1 > foo &&
	git add foo &&
	git cummit -m "$1"
}

test_expect_success 'setup' '

	cummit one &&
	cummit two &&
	git checkout HEAD^ &&
	cummit detached

'

test_expect_success 'rev-list --all lists detached HEAD' '

	test 3 = $(git rev-list --all | wc -l)

'

test_expect_success 'repack does not lose detached HEAD' '

	git gc &&
	git prune --expire=now &&
	git show HEAD

'

test_expect_success 'rev-list --graph --no-walk is forbidden' '
	test_must_fail git rev-list --graph --no-walk HEAD
'

test_done
