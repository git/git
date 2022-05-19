#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='ensure rebase fast-forwards cummits when possible'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_cummit A &&
	test_cummit B &&
	test_cummit C &&
	test_cummit D &&
	but checkout -t -b side
'

test_rebase_same_head () {
	status_n="$1" &&
	shift &&
	what_n="$1" &&
	shift &&
	cmp_n="$1" &&
	shift &&
	status_f="$1" &&
	shift &&
	what_f="$1" &&
	shift &&
	cmp_f="$1" &&
	shift &&
	test_rebase_same_head_ $status_n $what_n $cmp_n 0 " --apply" "$*" &&
	test_rebase_same_head_ $status_f $what_f $cmp_f 0 " --apply --no-ff" "$*"
	test_rebase_same_head_ $status_n $what_n $cmp_n 0 " --merge" "$*" &&
	test_rebase_same_head_ $status_f $what_f $cmp_f 0 " --merge --no-ff" "$*"
	test_rebase_same_head_ $status_n $what_n $cmp_n 1 " --merge" "$*" &&
	test_rebase_same_head_ $status_f $what_f $cmp_f 1 " --merge --no-ff" "$*"
}

test_rebase_same_head_ () {
	status="$1" &&
	shift &&
	what="$1" &&
	shift &&
	cmp="$1" &&
	shift &&
	abbreviate="$1" &&
	shift &&
	flag="$1"
	shift &&
	if test $abbreviate -eq 1
	then
		msg="but rebase$flag $* (rebase.abbreviateCommands = true) with $changes is $what with $cmp HEAD"
	else
		msg="but rebase$flag $* with $changes is $what with $cmp HEAD"
	fi &&
	test_expect_$status "$msg" "
		if test $abbreviate -eq 1
		then
			test_config rebase.abbreviateCommands true
		fi &&
		oldhead=\$(but rev-parse HEAD) &&
		test_when_finished 'but reset --hard \$oldhead' &&
		but reflog HEAD >expect &&
		but rebase$flag $* >stdout &&
		but reflog HEAD >actual &&
		if test $what = work
		then
			old=\$(wc -l <expect) &&
			test_line_count '-gt' \$old actual
		elif test $what = noop
		then
			test_cmp expect actual
		fi &&
		newhead=\$(but rev-parse HEAD) &&
		if test $cmp = same
		then
			test_cmp_rev \$oldhead \$newhead
		elif test $cmp = diff
		then
			test_cmp_rev ! \$oldhead \$newhead
		fi
	"
}

changes='no changes'
test_rebase_same_head success noop same success work same
test_rebase_same_head success noop same success work same main
test_rebase_same_head success noop same success work diff --onto B B
test_rebase_same_head success noop same success work diff --onto B... B
test_rebase_same_head success noop same success work same --onto main... main
test_rebase_same_head success noop same success work same --keep-base main
test_rebase_same_head success noop same success work same --keep-base
test_rebase_same_head success noop same success work same --no-fork-point
test_rebase_same_head success noop same success work same --keep-base --no-fork-point
test_rebase_same_head success noop same success work same --fork-point main
test_rebase_same_head success noop same success work diff --fork-point --onto B B
test_rebase_same_head success noop same success work diff --fork-point --onto B... B
test_rebase_same_head success noop same success work same --fork-point --onto main... main
test_rebase_same_head success noop same success work same --keep-base --keep-base main

test_expect_success 'add work same to side' '
	test_cummit E
'

changes='our changes'
test_rebase_same_head success noop same success work same
test_rebase_same_head success noop same success work same main
test_rebase_same_head success noop same success work diff --onto B B
test_rebase_same_head success noop same success work diff --onto B... B
test_rebase_same_head success noop same success work same --onto main... main
test_rebase_same_head success noop same success work same --keep-base main
test_rebase_same_head success noop same success work same --keep-base
test_rebase_same_head success noop same success work same --no-fork-point
test_rebase_same_head success noop same success work same --keep-base --no-fork-point
test_rebase_same_head success noop same success work same --fork-point main
test_rebase_same_head success noop same success work diff --fork-point --onto B B
test_rebase_same_head success noop same success work diff --fork-point --onto B... B
test_rebase_same_head success noop same success work same --fork-point --onto main... main
test_rebase_same_head success noop same success work same --fork-point --keep-base main

test_expect_success 'add work same to upstream' '
	but checkout main &&
	test_cummit F &&
	but checkout side
'

changes='our and their changes'
test_rebase_same_head success noop same success work diff --onto B B
test_rebase_same_head success noop same success work diff --onto B... B
test_rebase_same_head success noop same success work diff --onto main... main
test_rebase_same_head success noop same success work diff --keep-base main
test_rebase_same_head success noop same success work diff --keep-base
test_rebase_same_head failure work same success work diff --fork-point --onto B B
test_rebase_same_head failure work same success work diff --fork-point --onto B... B
test_rebase_same_head success noop same success work diff --fork-point --onto main... main
test_rebase_same_head success noop same success work diff --fork-point --keep-base main

test_done
