#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='ensure rebase fast-forwards commits when possible'

. ./test-lib.sh

test_expect_success setup '
	test_commit A &&
	test_commit B &&
	test_commit C &&
	test_commit D &&
	git checkout -t -b side
'

test_rebase_same_head () {
	status="$1" &&
	shift &&
	test_expect_$status "git rebase $* with $changes is no-op" "
		oldhead=\$(git rev-parse HEAD) &&
		test_when_finished 'git reset --hard \$oldhead' &&
		git rebase $* &&
		newhead=\$(git rev-parse HEAD) &&
		test_cmp_rev \$oldhead \$newhead
	"
}

changes='no changes'
test_rebase_same_head success
test_rebase_same_head success master
test_rebase_same_head success --onto B B
test_rebase_same_head success --onto B... B
test_rebase_same_head success --onto master... master
test_rebase_same_head success --keep-base master
test_rebase_same_head success --keep-base
test_rebase_same_head success --no-fork-point
test_rebase_same_head success --keep-base --no-fork-point
test_rebase_same_head success --fork-point master
test_rebase_same_head success --fork-point --onto B B
test_rebase_same_head success --fork-point --onto B... B
test_rebase_same_head success --fork-point --onto master... master
test_rebase_same_head success --fork-point --keep-base master

test_expect_success 'add work to side' '
	test_commit E
'

changes='our changes'
test_rebase_same_head success
test_rebase_same_head success master
test_rebase_same_head success --onto B B
test_rebase_same_head success --onto B... B
test_rebase_same_head success --onto master... master
test_rebase_same_head success --keep-base master
test_rebase_same_head success --keep-base
test_rebase_same_head success --no-fork-point
test_rebase_same_head success --keep-base --no-fork-point
test_rebase_same_head success --fork-point master
test_rebase_same_head success --fork-point --onto B B
test_rebase_same_head success --fork-point --onto B... B
test_rebase_same_head success --fork-point --onto master... master
test_rebase_same_head success --fork-point --keep-base master

test_expect_success 'add work to upstream' '
	git checkout master &&
	test_commit F &&
	git checkout side
'

changes='our and their changes'
test_rebase_same_head success --onto B B
test_rebase_same_head success --onto B... B
test_rebase_same_head success --onto master... master
test_rebase_same_head success --keep-base master
test_rebase_same_head success --keep-base
test_rebase_same_head success --fork-point --onto B B
test_rebase_same_head success --fork-point --onto B... B
test_rebase_same_head success --fork-point --onto master... master
test_rebase_same_head success --fork-point --keep-base master

test_done
