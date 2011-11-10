#!/bin/sh

test_description='git merge

Testing octopus merge when reducing parents to independent branches.'

. ./test-lib.sh

# 0 - 1
#   \ 2
#   \ 3
#   \ 4 - 5
#
# So 1, 2, 3 and 5 should be kept, 4 should be avoided.

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 > c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 > c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	git reset --hard c0 &&
	echo c3 > c3.c &&
	git add c3.c &&
	git commit -m c3 &&
	git tag c3 &&
	git reset --hard c0 &&
	echo c4 > c4.c &&
	git add c4.c &&
	git commit -m c4 &&
	git tag c4 &&
	echo c5 > c5.c &&
	git add c5.c &&
	git commit -m c5 &&
	git tag c5
'

test_expect_success 'merge c1 with c2, c3, c4, c5' '
	git reset --hard c1 &&
	git merge c2 c3 c4 c5 &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD^1)" &&
	test "$(git rev-parse c2)" = "$(git rev-parse HEAD^2)" &&
	test "$(git rev-parse c3)" = "$(git rev-parse HEAD^3)" &&
	test "$(git rev-parse c5)" = "$(git rev-parse HEAD^4)" &&
	git diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c &&
	test -f c4.c &&
	test -f c5.c
'

test_expect_success 'setup' '
	for i in A B C D E
	do
		echo $i > $i.c &&
		git add $i.c &&
		git commit -m $i &&
		git tag $i
	done &&
	git reset --hard A &&
	for i in F G H I
	do
		echo $i > $i.c &&
		git add $i.c &&
		git commit -m $i &&
		git tag $i
	done
'

test_expect_success 'merge E and I' '
	git reset --hard A &&
	git merge E I
'

test_expect_success 'verify merge result' '
	test $(git rev-parse HEAD^1) = $(git rev-parse E) &&
	test $(git rev-parse HEAD^2) = $(git rev-parse I)
'

test_expect_success 'add conflicts' '
	git reset --hard E &&
	echo foo > file.c &&
	git add file.c &&
	git commit -m E2 &&
	git tag E2 &&
	git reset --hard I &&
	echo bar >file.c &&
	git add file.c &&
	git commit -m I2 &&
	git tag I2
'

test_expect_success 'merge E2 and I2, causing a conflict and resolve it' '
	git reset --hard A &&
	test_must_fail git merge E2 I2 &&
	echo baz > file.c &&
	git add file.c &&
	git commit -m "resolve conflict"
'

test_expect_success 'verify merge result' '
	test $(git rev-parse HEAD^1) = $(git rev-parse E2) &&
	test $(git rev-parse HEAD^2) = $(git rev-parse I2)
'
test_done
