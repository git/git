#!/bin/sh

test_description='checkout can switch to last branch and merge base'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit initial world hello &&
	but branch other &&
	test_cummit --append second world "hello again"
'

test_expect_success '"checkout -" does not work initially' '
	test_must_fail but checkout -
'

test_expect_success 'first branch switch' '
	but checkout other
'

test_cmp_symbolic_HEAD_ref () {
	echo refs/heads/"$1" >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
}

test_expect_success '"checkout -" switches back' '
	but checkout - &&
	test_cmp_symbolic_HEAD_ref main
'

test_expect_success '"checkout -" switches forth' '
	but checkout - &&
	test_cmp_symbolic_HEAD_ref other
'

test_expect_success 'detach HEAD' '
	but checkout $(but rev-parse HEAD)
'

test_expect_success '"checkout -" attaches again' '
	but checkout - &&
	test_cmp_symbolic_HEAD_ref other
'

test_expect_success '"checkout -" detaches again' '
	but checkout - &&

	but rev-parse other >expect &&
	but rev-parse HEAD >actual &&
	test_cmp expect actual &&

	test_must_fail but symbolic-ref HEAD
'

test_expect_success 'more switches' '
	for i in 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1
	do
		but checkout -b branch$i || return 1
	done
'

more_switches () {
	for i in 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1
	do
		but checkout branch$i || return 1
	done
}

test_expect_success 'switch to the last' '
	more_switches &&
	but checkout @{-1} &&
	test_cmp_symbolic_HEAD_ref branch2
'

test_expect_success 'switch to second from the last' '
	more_switches &&
	but checkout @{-2} &&
	test_cmp_symbolic_HEAD_ref branch3
'

test_expect_success 'switch to third from the last' '
	more_switches &&
	but checkout @{-3} &&
	test_cmp_symbolic_HEAD_ref branch4
'

test_expect_success 'switch to fourth from the last' '
	more_switches &&
	but checkout @{-4} &&
	test_cmp_symbolic_HEAD_ref branch5
'

test_expect_success 'switch to twelfth from the last' '
	more_switches &&
	but checkout @{-12} &&
	test_cmp_symbolic_HEAD_ref branch13
'

test_expect_success 'merge base test setup' '
	but checkout -b another other &&
	test_cummit --append third world "hello again"
'

test_expect_success 'another...main' '
	but checkout another &&
	but checkout another...main &&

	but rev-parse --verify main^ >expect &&
	but rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '...main' '
	but checkout another &&
	but checkout ...main &&

	but rev-parse --verify main^ >expect &&
	but rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'main...' '
	but checkout another &&
	but checkout main... &&

	but rev-parse --verify main^ >expect &&
	but rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '"checkout -" works after a rebase A' '
	but checkout main &&
	but checkout other &&
	but rebase main &&
	but checkout - &&
	test_cmp_symbolic_HEAD_ref main
'

test_expect_success '"checkout -" works after a rebase A B' '
	but branch moodle main~1 &&
	but checkout main &&
	but checkout other &&
	but rebase main moodle &&
	but checkout - &&
	test_cmp_symbolic_HEAD_ref main
'

test_expect_success '"checkout -" works after a rebase -i A' '
	but checkout main &&
	but checkout other &&
	but rebase -i main &&
	but checkout - &&
	test_cmp_symbolic_HEAD_ref main
'

test_expect_success '"checkout -" works after a rebase -i A B' '
	but branch foodle main~1 &&
	but checkout main &&
	but checkout other &&
	but rebase main foodle &&
	but checkout - &&
	test_cmp_symbolic_HEAD_ref main
'

test_done
