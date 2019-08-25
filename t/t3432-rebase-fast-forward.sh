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
	what="$1" &&
	shift &&
	cmp="$1" &&
	shift &&
	test_expect_$status "git rebase $* with $changes is $what" "
		oldhead=\$(git rev-parse HEAD) &&
		test_when_finished 'git reset --hard \$oldhead' &&
		git rebase $* >stdout &&
		if test $what = work
		then
			test_i18ngrep 'rewinding head' stdout
		elif test $what = noop
		then
			test_i18ngrep 'is up to date' stdout
		fi &&
		newhead=\$(git rev-parse HEAD) &&
		if test $cmp = same
		then
			test_cmp_rev \$oldhead \$newhead
		elif test $cmp = diff
		then
			! test_cmp_rev \$oldhead \$newhead
		fi
	"
}

changes='no changes'
test_rebase_same_head success work same
test_rebase_same_head success noop same master
test_rebase_same_head success noop same --onto B B
test_rebase_same_head success noop same --onto B... B
test_rebase_same_head success noop same --onto master... master
test_rebase_same_head success noop same --no-fork-point
test_rebase_same_head success work same --fork-point master
test_rebase_same_head failure noop same --fork-point --onto B B
test_rebase_same_head failure work same --fork-point --onto B... B
test_rebase_same_head success work same --fork-point --onto master... master

test_expect_success 'add work same to side' '
	test_commit E
'

changes='our changes'
test_rebase_same_head success work same
test_rebase_same_head success noop same master
test_rebase_same_head success noop same --onto B B
test_rebase_same_head success noop same --onto B... B
test_rebase_same_head success noop same --onto master... master
test_rebase_same_head success noop same --no-fork-point
test_rebase_same_head success work same --fork-point master
test_rebase_same_head failure work same --fork-point --onto B B
test_rebase_same_head failure work same --fork-point --onto B... B
test_rebase_same_head success work same --fork-point --onto master... master

test_expect_success 'add work same to upstream' '
	git checkout master &&
	test_commit F &&
	git checkout side
'

changes='our and their changes'
test_rebase_same_head success noop same --onto B B
test_rebase_same_head success noop same --onto B... B
test_rebase_same_head failure work same --onto master... master
test_rebase_same_head failure work same --fork-point --onto B B
test_rebase_same_head failure work same --fork-point --onto B... B
test_rebase_same_head failure work same --fork-point --onto master... master

test_done
