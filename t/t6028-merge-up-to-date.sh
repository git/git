#!/bin/sh

test_description='merge fast forward and up to date'

. ./test-lib.sh

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag c0 &&

	echo second >file &&
	git add file &&
	test_tick &&
	git commit -m second &&
	git tag c1 &&
	git branch test
'

test_expect_success 'merge -s recursive up-to-date' '

	git reset --hard c1 &&
	test_tick &&
	git merge -s recursive c0 &&
	expect=$(git rev-parse c1) &&
	current=$(git rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s recursive fast-forward' '

	git reset --hard c0 &&
	test_tick &&
	git merge -s recursive c1 &&
	expect=$(git rev-parse c1) &&
	current=$(git rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s ours up-to-date' '

	git reset --hard c1 &&
	test_tick &&
	git merge -s ours c0 &&
	expect=$(git rev-parse c1) &&
	current=$(git rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s ours fast-forward' '

	git reset --hard c0 &&
	test_tick &&
	git merge -s ours c1 &&
	expect=$(git rev-parse c0^{tree}) &&
	current=$(git rev-parse HEAD^{tree}) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s subtree up-to-date' '

	git reset --hard c1 &&
	test_tick &&
	git merge -s subtree c0 &&
	expect=$(git rev-parse c1) &&
	current=$(git rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_done
