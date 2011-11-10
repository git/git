#!/bin/sh

test_description='git merge

Testing octopus merge with more than 25 refs.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	i=1 &&
	while test $i -le 30
	do
		git reset --hard c0 &&
		echo c$i > c$i.c &&
		git add c$i.c &&
		git commit -m c$i &&
		git tag c$i &&
		i=`expr $i + 1` || return 1
	done
'

test_expect_success 'merge c1 with c2, c3, c4, ... c29' '
	git reset --hard c1 &&
	i=2 &&
	refs="" &&
	while test $i -le 30
	do
		refs="$refs c$i"
		i=`expr $i + 1`
	done
	git merge $refs &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	i=1 &&
	while test $i -le 30
	do
		test "$(git rev-parse c$i)" = "$(git rev-parse HEAD^$i)" &&
		i=`expr $i + 1` || return 1
	done &&
	git diff --exit-code &&
	i=1 &&
	while test $i -le 30
	do
		test -f c$i.c &&
		i=`expr $i + 1` || return 1
	done
'

test_done
