#!/bin/sh

test_description='merge fast-forward and up to date'

. ./test-lib.sh

test_expect_success setup '
	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but tag c0 &&

	echo second >file &&
	but add file &&
	test_tick &&
	but cummit -m second &&
	but tag c1 &&
	but branch test &&
	echo third >file &&
	but add file &&
	test_tick &&
	but cummit -m third &&
	but tag c2
'

test_expect_success 'merge -s recursive up-to-date' '

	but reset --hard c1 &&
	test_tick &&
	but merge -s recursive c0 &&
	expect=$(but rev-parse c1) &&
	current=$(but rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s recursive fast-forward' '

	but reset --hard c0 &&
	test_tick &&
	but merge -s recursive c1 &&
	expect=$(but rev-parse c1) &&
	current=$(but rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s ours up-to-date' '

	but reset --hard c1 &&
	test_tick &&
	but merge -s ours c0 &&
	expect=$(but rev-parse c1) &&
	current=$(but rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s ours fast-forward' '

	but reset --hard c0 &&
	test_tick &&
	but merge -s ours c1 &&
	expect=$(but rev-parse c0^{tree}) &&
	current=$(but rev-parse HEAD^{tree}) &&
	test "$expect" = "$current"

'

test_expect_success 'merge -s subtree up-to-date' '

	but reset --hard c1 &&
	test_tick &&
	but merge -s subtree c0 &&
	expect=$(but rev-parse c1) &&
	current=$(but rev-parse HEAD) &&
	test "$expect" = "$current"

'

test_expect_success 'merge fast-forward octopus' '

	but reset --hard c0 &&
	test_tick &&
	but merge c1 c2 &&
	expect=$(but rev-parse c2) &&
	current=$(but rev-parse HEAD) &&
	test "$expect" = "$current"
'

test_done
