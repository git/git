#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='git rebase --fork-point test'

. ./test-lib.sh

# A---B---D---E    (master)
#      \
#       C*---F---G (side)
#
# C was formerly part of master but master was rewound to remove C
#
test_expect_success setup '
	test_commit A &&
	test_commit B &&
	test_commit C &&
	git branch -t side &&
	git reset --hard HEAD^ &&
	test_commit D &&
	test_commit E &&
	git checkout side &&
	test_commit F &&
	test_commit G
'

test_rebase () {
	expected="$1" &&
	shift &&
	test_expect_success "git rebase $*" "
		git checkout master &&
		git reset --hard E &&
		git checkout side &&
		git reset --hard G &&
		git rebase $* &&
		test_write_lines $expected >expect &&
		git log --pretty=%s >actual &&
		test_cmp expect actual
	"
}

test_rebase 'G F E D B A'
test_rebase 'G F D B A' --onto D
test_rebase 'G F B A' --keep-base
test_rebase 'G F C E D B A' --no-fork-point
test_rebase 'G F C D B A' --no-fork-point --onto D
test_rebase 'G F C B A' --no-fork-point --keep-base
test_rebase 'G F E D B A' --fork-point refs/heads/master
test_rebase 'G F D B A' --fork-point --onto D refs/heads/master
test_rebase 'G F B A' --fork-point --keep-base refs/heads/master
test_rebase 'G F C E D B A' refs/heads/master
test_rebase 'G F C D B A' --onto D refs/heads/master
test_rebase 'G F C B A' --keep-base refs/heads/master

test_done
