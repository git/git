#!/bin/sh

test_description='but merge

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
	but add c0.c &&
	but cummit -m c0 &&
	but tag c0 &&
	echo c1 > c1.c &&
	but add c1.c &&
	but cummit -m c1 &&
	but tag c1 &&
	but reset --hard c0 &&
	echo c2 > c2.c &&
	but add c2.c &&
	but cummit -m c2 &&
	but tag c2 &&
	but reset --hard c0 &&
	echo c3 > c3.c &&
	but add c3.c &&
	but cummit -m c3 &&
	but tag c3 &&
	but reset --hard c0 &&
	echo c4 > c4.c &&
	but add c4.c &&
	but cummit -m c4 &&
	but tag c4 &&
	echo c5 > c5.c &&
	but add c5.c &&
	but cummit -m c5 &&
	but tag c5
'

test_expect_success 'merge c1 with c2, c3, c4, c5' '
	but reset --hard c1 &&
	but merge c2 c3 c4 c5 &&
	test "$(but rev-parse c1)" != "$(but rev-parse HEAD)" &&
	test "$(but rev-parse c1)" = "$(but rev-parse HEAD^1)" &&
	test "$(but rev-parse c2)" = "$(but rev-parse HEAD^2)" &&
	test "$(but rev-parse c3)" = "$(but rev-parse HEAD^3)" &&
	test "$(but rev-parse c5)" = "$(but rev-parse HEAD^4)" &&
	but diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c &&
	test -f c4.c &&
	test -f c5.c &&
	but show --format=%s -s >actual &&
	! grep c1 actual &&
	grep c2 actual &&
	grep c3 actual &&
	! grep c4 actual &&
	grep c5 actual
'

test_expect_success 'pull c2, c3, c4, c5 into c1' '
	but reset --hard c1 &&
	but pull --no-rebase . c2 c3 c4 c5 &&
	test "$(but rev-parse c1)" != "$(but rev-parse HEAD)" &&
	test "$(but rev-parse c1)" = "$(but rev-parse HEAD^1)" &&
	test "$(but rev-parse c2)" = "$(but rev-parse HEAD^2)" &&
	test "$(but rev-parse c3)" = "$(but rev-parse HEAD^3)" &&
	test "$(but rev-parse c5)" = "$(but rev-parse HEAD^4)" &&
	but diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c &&
	test -f c4.c &&
	test -f c5.c &&
	but show --format=%s -s >actual &&
	! grep c1 actual &&
	grep c2 actual &&
	grep c3 actual &&
	! grep c4 actual &&
	grep c5 actual
'

test_expect_success 'setup' '
	for i in A B C D E
	do
		echo $i > $i.c &&
		but add $i.c &&
		but cummit -m $i &&
		but tag $i || return 1
	done &&
	but reset --hard A &&
	for i in F G H I
	do
		echo $i > $i.c &&
		but add $i.c &&
		but cummit -m $i &&
		but tag $i || return 1
	done
'

test_expect_success 'merge E and I' '
	but reset --hard A &&
	but merge E I
'

test_expect_success 'verify merge result' '
	test $(but rev-parse HEAD^1) = $(but rev-parse E) &&
	test $(but rev-parse HEAD^2) = $(but rev-parse I)
'

test_expect_success 'add conflicts' '
	but reset --hard E &&
	echo foo > file.c &&
	but add file.c &&
	but cummit -m E2 &&
	but tag E2 &&
	but reset --hard I &&
	echo bar >file.c &&
	but add file.c &&
	but cummit -m I2 &&
	but tag I2
'

test_expect_success 'merge E2 and I2, causing a conflict and resolve it' '
	but reset --hard A &&
	test_must_fail but merge E2 I2 &&
	echo baz > file.c &&
	but add file.c &&
	but cummit -m "resolve conflict"
'

test_expect_success 'verify merge result' '
	test $(but rev-parse HEAD^1) = $(but rev-parse E2) &&
	test $(but rev-parse HEAD^2) = $(but rev-parse I2)
'

test_expect_success 'fast-forward to redundant refs' '
	but reset --hard c0 &&
	but merge c4 c5
'

test_expect_success 'verify merge result' '
	test $(but rev-parse HEAD) = $(but rev-parse c5)
'

test_expect_success 'merge up-to-date redundant refs' '
	but reset --hard c5 &&
	but merge c0 c4
'

test_expect_success 'verify merge result' '
	test $(but rev-parse HEAD) = $(but rev-parse c5)
'

test_done
