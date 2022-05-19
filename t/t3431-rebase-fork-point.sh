#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='but rebase --fork-point test'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# A---B---D---E    (main)
#      \
#       C*---F---G (side)
#
# C was formerly part of main but main was rewound to remove C
#
test_expect_success setup '
	test_cummit A &&
	test_cummit B &&
	test_cummit C &&
	but branch -t side &&
	but reset --hard HEAD^ &&
	test_cummit D &&
	test_cummit E &&
	but checkout side &&
	test_cummit F &&
	test_cummit G
'

do_test_rebase () {
	expected="$1" &&
	shift &&
	but checkout main &&
	but reset --hard E &&
	but checkout side &&
	but reset --hard G &&
	but rebase $* &&
	test_write_lines $expected >expect &&
	but log --pretty=%s >actual &&
	test_cmp expect actual
}

test_rebase () {
	expected="$1" &&
	shift &&
	test_expect_success "but rebase $*" "do_test_rebase '$expected' $*"
}

test_rebase 'G F E D B A'
test_rebase 'G F D B A' --onto D
test_rebase 'G F B A' --keep-base
test_rebase 'G F C E D B A' --no-fork-point
test_rebase 'G F C D B A' --no-fork-point --onto D
test_rebase 'G F C B A' --no-fork-point --keep-base

test_rebase 'G F E D B A' --fork-point refs/heads/main
test_rebase 'G F E D B A' --fork-point main

test_rebase 'G F D B A' --fork-point --onto D refs/heads/main
test_rebase 'G F D B A' --fork-point --onto D main

test_rebase 'G F B A' --fork-point --keep-base refs/heads/main
test_rebase 'G F B A' --fork-point --keep-base main

test_rebase 'G F C E D B A' refs/heads/main
test_rebase 'G F C E D B A' main

test_rebase 'G F C D B A' --onto D refs/heads/main
test_rebase 'G F C D B A' --onto D main

test_rebase 'G F C B A' --keep-base refs/heads/main
test_rebase 'G F C B A' --keep-base main

test_expect_success 'but rebase --fork-point with ambigous refname' '
	but checkout main &&
	but checkout -b one &&
	but checkout side &&
	but tag one &&
	test_must_fail but rebase --fork-point --onto D one
'

test_expect_success '--fork-point and --root both given' '
	test_must_fail but rebase --fork-point --root 2>err &&
	test_i18ngrep "cannot be used together" err
'

test_expect_success 'rebase.forkPoint set to false' '
	test_config rebase.forkPoint false &&
	do_test_rebase "G F C E D B A"
'

test_expect_success 'rebase.forkPoint set to false and then to true' '
	test_config_global rebase.forkPoint false &&
	test_config rebase.forkPoint true &&
	do_test_rebase "G F E D B A"
'

test_expect_success 'rebase.forkPoint set to false and command line says --fork-point' '
	test_config rebase.forkPoint false &&
	do_test_rebase "G F E D B A" --fork-point
'

test_expect_success 'rebase.forkPoint set to true and command line says --no-fork-point' '
	test_config rebase.forkPoint true &&
	do_test_rebase "G F C E D B A" --no-fork-point
'

test_expect_success 'rebase.forkPoint set to true and --root given' '
	test_config rebase.forkPoint true &&
	but rebase --root
'

test_done
