#!/bin/sh
#
# Copyright (c) 2010 Christian Couder
#

test_description='Tests to check that "reset" options follow a known table'

. ./test-lib.sh


test_expect_success 'creating initial cummits' '
    test_cummit E file1 &&
    test_cummit D file1 &&
    test_cummit C file1
'

while read W1 I1 H1 T opt W2 I2 H2
do
    test_expect_success "check: $W1 $I1 $H1 $T --$opt $W2 $I2 $H2" '
	but reset --hard C &&
	if test "$I1" != "$H1"
	then
	    echo "$I1" >file1 &&
	    but add file1
	fi &&
	if test "$W1" != "$I1"
	then
	    echo "$W1" >file1
	fi &&
	if test "$W2" != "XXXXX"
	then
	    but reset --$opt $T &&
	    test "$(cat file1)" = "$W2" &&
	    but checkout-index -f -- file1 &&
	    test "$(cat file1)" = "$I2" &&
	    but checkout -f HEAD -- file1 &&
	    test "$(cat file1)" = "$H2"
	else
	    test_must_fail but reset --$opt $T
	fi
    '
done <<\EOF
A B C D soft   A B D
A B C D mixed  A D D
A B C D hard   D D D
A B C D merge  XXXXX
A B C D keep   XXXXX
A B C C soft   A B C
A B C C mixed  A C C
A B C C hard   C C C
A B C C merge  XXXXX
A B C C keep   A C C
B B C D soft   B B D
B B C D mixed  B D D
B B C D hard   D D D
B B C D merge  D D D
B B C D keep   XXXXX
B B C C soft   B B C
B B C C mixed  B C C
B B C C hard   C C C
B B C C merge  C C C
B B C C keep   B C C
B C C D soft   B C D
B C C D mixed  B D D
B C C D hard   D D D
B C C D merge  XXXXX
B C C D keep   XXXXX
B C C C soft   B C C
B C C C mixed  B C C
B C C C hard   C C C
B C C C merge  B C C
B C C C keep   B C C
EOF

test_expect_success 'setting up branches to test with unmerged entries' '
    but reset --hard C &&
    but branch branch1 &&
    but branch branch2 &&
    but checkout branch1 &&
    test_cummit B1 file1 &&
    but checkout branch2 &&
    test_cummit B file1
'

while read W1 I1 H1 T opt W2 I2 H2
do
    test_expect_success "check: $W1 $I1 $H1 $T --$opt $W2 $I2 $H2" '
	but reset --hard B &&
	test_must_fail but merge branch1 &&
	cat file1 >X_file1 &&
	if test "$W2" != "XXXXX"
	then
	    but reset --$opt $T &&
	    if test "$W2" = "X"
	    then
		test_cmp file1 X_file1
	    else
		test "$(cat file1)" = "$W2"
	    fi &&
	    but checkout-index -f -- file1 &&
	    test "$(cat file1)" = "$I2" &&
	    but checkout -f HEAD -- file1 &&
	    test "$(cat file1)" = "$H2"
	else
	    test_must_fail but reset --$opt $T
	fi
    '
done <<\EOF
X U B C soft   XXXXX
X U B C mixed  X C C
X U B C hard   C C C
X U B C merge  C C C
X U B C keep   XXXXX
X U B B soft   XXXXX
X U B B mixed  X B B
X U B B hard   B B B
X U B B merge  B B B
X U B B keep   XXXXX
EOF

test_done
