#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test but rev-parse with different parent options'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_cmp_rev_output () {
	but rev-parse --verify "$1" >expect &&
	eval "$2" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	test_cummit start &&
	test_cummit second &&
	but checkout --orphan tmp &&
	test_cummit start2 &&
	but checkout main &&
	but merge -m next --allow-unrelated-histories start2 &&
	test_cummit final &&

	test_seq 40 |
	while read i
	do
		but checkout --orphan "b$i" &&
		test_tick &&
		but cummit --allow-empty -m "$i" &&
		cummit=$(but rev-parse --verify HEAD) &&
		printf "$cummit " >>.but/info/grafts || return 1
	done
'

test_expect_success 'start is valid' '
	but rev-parse start | grep "^$OID_REGEX$"
'

test_expect_success 'start^0' '
	test_cmp_rev_output tags/start "but rev-parse start^0"
'

test_expect_success 'start^1 not valid' '
	test_must_fail but rev-parse --verify start^1
'

test_expect_success 'second^1 = second^' '
	test_cmp_rev_output second^ "but rev-parse second^1"
'

test_expect_success 'final^1^1^1' '
	test_cmp_rev_output start "but rev-parse final^1^1^1"
'

test_expect_success 'final^1^1^1 = final^^^' '
	test_cmp_rev_output final^^^ "but rev-parse final^1^1^1"
'

test_expect_success 'final^1^2' '
	test_cmp_rev_output start2 "but rev-parse final^1^2"
'

test_expect_success 'final^1^2 != final^1^1' '
	test $(but rev-parse final^1^2) != $(but rev-parse final^1^1)
'

test_expect_success 'final^1^3 not valid' '
	test_must_fail but rev-parse --verify final^1^3
'

test_expect_success '--verify start2^1' '
	test_must_fail but rev-parse --verify start2^1
'

test_expect_success '--verify start2^0' '
	but rev-parse --verify start2^0
'

test_expect_success 'final^1^@ = final^1^1 final^1^2' '
	but rev-parse final^1^1 final^1^2 >expect &&
	but rev-parse final^1^@ >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic final^1^@ = final^1^1 final^1^2' '
	but rev-parse --symbolic final^1^1 final^1^2 >expect &&
	but rev-parse --symbolic final^1^@ >actual &&
	test_cmp expect actual
'

test_expect_success 'final^1^! = final^1 ^final^1^1 ^final^1^2' '
	but rev-parse final^1 ^final^1^1 ^final^1^2 >expect &&
	but rev-parse final^1^! >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic final^1^! = final^1 ^final^1^1 ^final^1^2' '
	but rev-parse --symbolic final^1 ^final^1^1 ^final^1^2 >expect &&
	but rev-parse --symbolic final^1^! >actual &&
	test_cmp expect actual
'

test_expect_success 'large graft octopus' '
	test_cmp_rev_output b31 "but rev-parse --verify b1^30"
'

test_expect_success 'repack for next test' '
	but repack -a -d
'

test_expect_success 'short SHA-1 works' '
	start=$(but rev-parse --verify start) &&
	test_cmp_rev_output start "but rev-parse ${start%?}"
'

# rev^- tests; we can use a simpler setup for these

test_expect_success 'setup for rev^- tests' '
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&

	# Merge in a branch for testing rev^-
	but checkout -b branch &&
	but checkout HEAD^^ &&
	but merge -m merge --no-edit --no-ff branch &&
	but checkout -b merge
'

# The merged branch has 2 cummits + the merge
test_expect_success 'rev-list --count merge^- = merge^..merge' '
	but rev-list --count merge^..merge >expect &&
	echo 3 >actual &&
	test_cmp expect actual
'

# All rev^- rev-parse tests

test_expect_success 'rev-parse merge^- = merge^..merge' '
	but rev-parse merge^..merge >expect &&
	but rev-parse merge^- >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-1 = merge^..merge' '
	but rev-parse merge^1..merge >expect &&
	but rev-parse merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-2 = merge^2..merge' '
	but rev-parse merge^2..merge >expect &&
	but rev-parse merge^-2 >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic merge^-1 = merge^1..merge' '
	but rev-parse --symbolic merge^1..merge >expect &&
	but rev-parse --symbolic merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-0 (invalid parent)' '
	test_must_fail but rev-parse merge^-0
'

test_expect_success 'rev-parse merge^-3 (invalid parent)' '
	test_must_fail but rev-parse merge^-3
'

test_expect_success 'rev-parse merge^-^ (garbage after ^-)' '
	test_must_fail but rev-parse merge^-^
'

test_expect_success 'rev-parse merge^-1x (garbage after ^-1)' '
	test_must_fail but rev-parse merge^-1x
'

# All rev^- rev-list tests (should be mostly the same as rev-parse; the reason
# for the duplication is that rev-parse and rev-list use different parsers).

test_expect_success 'rev-list merge^- = merge^..merge' '
	but rev-list merge^..merge >expect &&
	but rev-list merge^- >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-1 = merge^1..merge' '
	but rev-list merge^1..merge >expect &&
	but rev-list merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-2 = merge^2..merge' '
	but rev-list merge^2..merge >expect &&
	but rev-list merge^-2 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-0 (invalid parent)' '
	test_must_fail but rev-list merge^-0
'

test_expect_success 'rev-list merge^-3 (invalid parent)' '
	test_must_fail but rev-list merge^-3
'

test_expect_success 'rev-list merge^-^ (garbage after ^-)' '
	test_must_fail but rev-list merge^-^
'

test_expect_success 'rev-list merge^-1x (garbage after ^-1)' '
	test_must_fail but rev-list merge^-1x
'

test_expect_success 'rev-parse $garbage^@ does not segfault' '
	test_must_fail but rev-parse $EMPTY_TREE^@
'

test_expect_success 'rev-parse $garbage...$garbage does not segfault' '
	test_must_fail but rev-parse $EMPTY_TREE...$EMPTY_BLOB
'

test_done
