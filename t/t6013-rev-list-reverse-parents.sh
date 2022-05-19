#!/bin/sh

test_description='--reverse combines with --parents'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


cummit () {
	test_tick &&
	echo $1 > foo &&
	but add foo &&
	but cummit -m "$1"
}

test_expect_success 'set up --reverse example' '
	cummit one &&
	but tag root &&
	cummit two &&
	but checkout -b side HEAD^ &&
	cummit three &&
	but checkout main &&
	but merge -s ours side &&
	cummit five
	'

test_expect_success '--reverse --parents --full-history combines correctly' '
	but rev-list --parents --full-history main -- foo |
		perl -e "print reverse <>" > expected &&
	but rev-list --reverse --parents --full-history main -- foo \
		> actual &&
	test_cmp expected actual
	'

test_expect_success '--boundary does too' '
	but rev-list --boundary --parents --full-history main ^root -- foo |
		perl -e "print reverse <>" > expected &&
	but rev-list --boundary --reverse --parents --full-history \
		main ^root -- foo > actual &&
	test_cmp expected actual
	'

test_done
