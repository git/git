#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git rev-parse with different parent options'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-t6000.sh # t6xxx specific functions

test_cmp_rev_output () {
	git rev-parse --verify "$1" >expect &&
	eval "$2" >actual &&
	test_cmp expect actual
}

date >path0
git update-index --add path0
save_tag tree git write-tree
hide_error save_tag start unique_commit "start" tree
save_tag second unique_commit "second" tree -p start
hide_error save_tag start2 unique_commit "start2" tree
save_tag two_parents unique_commit "next" tree -p second -p start2
save_tag final unique_commit "final" tree -p two_parents

test_expect_success 'start is valid' '
	git rev-parse start | grep "^[0-9a-f]\{40\}$"
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

test_expect_success 'final^1^! = final^1 ^final^1^1 ^final^1^2' '
	git rev-parse final^1 ^final^1^1 ^final^1^2 >expect &&
	git rev-parse final^1^! >actual &&
	test_cmp expect actual
'

test_expect_success 'repack for next test' '
	git repack -a -d
'

test_expect_success 'short SHA-1 works' '
	start=$(git rev-parse --verify start) &&
	test_cmp_rev_output start "git rev-parse ${start%?}"
'

test_done
