#!/bin/sh

test_description='--reverse combines with --parents'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


commit () {
	test_tick &&
	echo $1 > foo &&
	git add foo &&
	git commit -m "$1"
}

test_expect_success 'set up --reverse example' '
	commit one &&
	git tag root &&
	commit two &&
	git checkout -b side HEAD^ &&
	commit three &&
	git checkout main &&
	git merge -s ours side &&
	commit five
	'

test_expect_success '--reverse --parents --full-history combines correctly' '
	git rev-list --parents --full-history main -- foo |
		perl -e "print reverse <>" > expected &&
	git rev-list --reverse --parents --full-history main -- foo \
		> actual &&
	test_cmp expected actual
	'

test_expect_success '--boundary does too' '
	git rev-list --boundary --parents --full-history main ^root -- foo |
		perl -e "print reverse <>" > expected &&
	git rev-list --boundary --reverse --parents --full-history \
		main ^root -- foo > actual &&
	test_cmp expected actual
	'

test_done
