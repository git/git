#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git rev-parse with different parent options'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_cmp_rev_output () {
	git rev-parse --verify "$1" >expect &&
	eval "$2" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	test_commit start &&
	test_commit second &&
	git checkout --orphan tmp &&
	test_commit start2 &&
	git checkout main &&
	git merge -m next --allow-unrelated-histories start2 &&
	test_commit final &&

	test_seq 40 |
	while read i
	do
		git checkout --orphan "b$i" &&
		test_tick &&
		git commit --allow-empty -m "$i" &&
		commit=$(git rev-parse --verify HEAD) &&
		printf "$commit " >>.git/info/grafts
	done
'

test_expect_success 'start is valid' '
	git rev-parse start | grep "^$OID_REGEX$"
'

test_expect_success 'start^0' '
	test_cmp_rev_output tags/start "git rev-parse start^0"
'

test_expect_success 'start^1 not valid' '
	test_must_fail git rev-parse --verify start^1
'

test_expect_success 'second^1 = second^' '
	test_cmp_rev_output second^ "git rev-parse second^1"
'

test_expect_success 'final^1^1^1' '
	test_cmp_rev_output start "git rev-parse final^1^1^1"
'

test_expect_success 'final^1^1^1 = final^^^' '
	test_cmp_rev_output final^^^ "git rev-parse final^1^1^1"
'

test_expect_success 'final^1^2' '
	test_cmp_rev_output start2 "git rev-parse final^1^2"
'

test_expect_success 'final^1^2 != final^1^1' '
	test $(git rev-parse final^1^2) != $(git rev-parse final^1^1)
'

test_expect_success 'final^1^3 not valid' '
	test_must_fail git rev-parse --verify final^1^3
'

test_expect_success '--verify start2^1' '
	test_must_fail git rev-parse --verify start2^1
'

test_expect_success '--verify start2^0' '
	git rev-parse --verify start2^0
'

test_expect_success 'final^1^@ = final^1^1 final^1^2' '
	git rev-parse final^1^1 final^1^2 >expect &&
	git rev-parse final^1^@ >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic final^1^@ = final^1^1 final^1^2' '
	git rev-parse --symbolic final^1^1 final^1^2 >expect &&
	git rev-parse --symbolic final^1^@ >actual &&
	test_cmp expect actual
'

test_expect_success 'final^1^! = final^1 ^final^1^1 ^final^1^2' '
	git rev-parse final^1 ^final^1^1 ^final^1^2 >expect &&
	git rev-parse final^1^! >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic final^1^! = final^1 ^final^1^1 ^final^1^2' '
	git rev-parse --symbolic final^1 ^final^1^1 ^final^1^2 >expect &&
	git rev-parse --symbolic final^1^! >actual &&
	test_cmp expect actual
'

test_expect_success 'large graft octopus' '
	test_cmp_rev_output b31 "git rev-parse --verify b1^30"
'

test_expect_success 'repack for next test' '
	git repack -a -d
'

test_expect_success 'short SHA-1 works' '
	start=$(git rev-parse --verify start) &&
	test_cmp_rev_output start "git rev-parse ${start%?}"
'

# rev^- tests; we can use a simpler setup for these

test_expect_success 'setup for rev^- tests' '
	test_commit one &&
	test_commit two &&
	test_commit three &&

	# Merge in a branch for testing rev^-
	git checkout -b branch &&
	git checkout HEAD^^ &&
	git merge -m merge --no-edit --no-ff branch &&
	git checkout -b merge
'

# The merged branch has 2 commits + the merge
test_expect_success 'rev-list --count merge^- = merge^..merge' '
	git rev-list --count merge^..merge >expect &&
	echo 3 >actual &&
	test_cmp expect actual
'

# All rev^- rev-parse tests

test_expect_success 'rev-parse merge^- = merge^..merge' '
	git rev-parse merge^..merge >expect &&
	git rev-parse merge^- >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-1 = merge^..merge' '
	git rev-parse merge^1..merge >expect &&
	git rev-parse merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-2 = merge^2..merge' '
	git rev-parse merge^2..merge >expect &&
	git rev-parse merge^-2 >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic merge^-1 = merge^1..merge' '
	git rev-parse --symbolic merge^1..merge >expect &&
	git rev-parse --symbolic merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse merge^-0 (invalid parent)' '
	test_must_fail git rev-parse merge^-0
'

test_expect_success 'rev-parse merge^-3 (invalid parent)' '
	test_must_fail git rev-parse merge^-3
'

test_expect_success 'rev-parse merge^-^ (garbage after ^-)' '
	test_must_fail git rev-parse merge^-^
'

test_expect_success 'rev-parse merge^-1x (garbage after ^-1)' '
	test_must_fail git rev-parse merge^-1x
'

# All rev^- rev-list tests (should be mostly the same as rev-parse; the reason
# for the duplication is that rev-parse and rev-list use different parsers).

test_expect_success 'rev-list merge^- = merge^..merge' '
	git rev-list merge^..merge >expect &&
	git rev-list merge^- >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-1 = merge^1..merge' '
	git rev-list merge^1..merge >expect &&
	git rev-list merge^-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-2 = merge^2..merge' '
	git rev-list merge^2..merge >expect &&
	git rev-list merge^-2 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list merge^-0 (invalid parent)' '
	test_must_fail git rev-list merge^-0
'

test_expect_success 'rev-list merge^-3 (invalid parent)' '
	test_must_fail git rev-list merge^-3
'

test_expect_success 'rev-list merge^-^ (garbage after ^-)' '
	test_must_fail git rev-list merge^-^
'

test_expect_success 'rev-list merge^-1x (garbage after ^-1)' '
	test_must_fail git rev-list merge^-1x
'

test_expect_success 'rev-parse $garbage^@ does not segfault' '
	test_must_fail git rev-parse $EMPTY_TREE^@
'

test_expect_success 'rev-parse $garbage...$garbage does not segfault' '
	test_must_fail git rev-parse $EMPTY_TREE...$EMPTY_BLOB
'

test_done
