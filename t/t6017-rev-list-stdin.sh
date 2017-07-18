#!/bin/sh
#
# Copyright (c) 2009, Junio C Hamano
#

test_description='log family learns --stdin'

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
			git checkout -b side-$i master~$i &&
			echo updated $i >file-$i &&
			git add file-$i &&
			test_tick &&
			git commit -m side-$i || exit
		done
	)
'

check master
check side-1 ^side-4
check side-1 ^side-7 --
check side-1 ^side-7 -- file-1
check side-1 ^side-7 -- file-2
check side-3 ^side-4 -- file-3
check side-3 ^side-2
check side-3 ^side-2 -- file-1

test_expect_success 'not only --stdin' '
	cat >expect <<-EOF &&
	7

	file-1
	file-2
	EOF
	cat >input <<-EOF &&
	^master^
	--
	file-2
	EOF
	git log --pretty=tformat:%s --name-only --stdin master -- file-1 \
		<input >actual &&
	test_cmp expect actual
'

test_done
