#!/bin/sh
#
# Copyright (c) 2009, Junio C Hamano
#

test_description='log family learns --stdin'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check () {
	for cmd in rev-list "log --stat"
	do
		for i in "$@"
		do
			printf "%s\n" $i
		done >input &&
		test_expect_success "check $cmd $*" '
			git $cmd $(cat input) >expect &&
			git $cmd --stdin <input >actual &&
			sed -e "s/^/input /" input &&
			sed -e "s/^/output /" expect &&
			test_cmp expect actual
		'
	done
}

them='1 2 3 4 5 6 7'

test_expect_success setup '
	(
		for i in 0 $them
		do
			for j in $them
			do
				echo $i.$j >file-$j &&
				git add file-$j || exit
			done &&
			test_tick &&
			git commit -m $i || exit
		done &&
		for i in $them
		do
			git checkout -b side-$i main~$i &&
			echo updated $i >file-$i &&
			git add file-$i &&
			test_tick &&
			git commit -m side-$i || exit
		done &&

		git update-ref refs/heads/-dashed-branch HEAD
	)
'

check main
check side-1 ^side-4
check side-1 ^side-7 --
check side-1 ^side-7 -- file-1
check side-1 ^side-7 -- file-2
check side-3 ^side-4 -- file-3
check side-3 ^side-2
check side-3 ^side-2 -- file-1
check --all
check --all --not --branches
check --glob=refs/heads
check --glob=refs/heads --
check --glob=refs/heads -- file-1
check --end-of-options -dashed-branch
check --all --not refs/heads/main

test_expect_success 'not only --stdin' '
	cat >expect <<-EOF &&
	7

	file-1
	file-2
	EOF
	cat >input <<-EOF &&
	^main^
	--
	file-2
	EOF
	git log --pretty=tformat:%s --name-only --stdin main -- file-1 \
		<input >actual &&
	test_cmp expect actual
'

test_expect_success 'pseudo-opt with missing value' '
	cat >input <<-EOF &&
	--glob
	refs/heads
	EOF

	cat >expect <<-EOF &&
	fatal: Option ${SQ}--glob${SQ} requires a value
	EOF

	test_must_fail git rev-list --stdin <input 2>error &&
	test_cmp expect error
'

test_expect_success 'pseudo-opt with invalid value' '
	cat >input <<-EOF &&
	--no-walk=garbage
	EOF

	cat >expect <<-EOF &&
	error: invalid argument to --no-walk
	fatal: invalid option ${SQ}--no-walk=garbage${SQ} in --stdin mode
	EOF

	test_must_fail git rev-list --stdin <input 2>error &&
	test_cmp expect error
'

test_expect_success 'unknown option without --end-of-options' '
	cat >input <<-EOF &&
	-dashed-branch
	EOF

	cat >expect <<-EOF &&
	fatal: invalid option ${SQ}-dashed-branch${SQ} in --stdin mode
	EOF

	test_must_fail git rev-list --stdin <input 2>error &&
	test_cmp expect error
'

test_expect_success '--not on command line does not influence revisions read via --stdin' '
	cat >input <<-EOF &&
	refs/heads/main
	EOF
	git rev-list refs/heads/main >expect &&

	git rev-list refs/heads/main --not --stdin <input >actual &&
	test_cmp expect actual
'

test_expect_success '--not via stdin does not influence revisions from command line' '
	cat >input <<-EOF &&
	--not
	EOF
	git rev-list refs/heads/main >expect &&

	git rev-list refs/heads/main --stdin refs/heads/main <input >actual &&
	test_cmp expect actual
'

test_done
