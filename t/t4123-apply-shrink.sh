#!/bin/sh

test_description='apply a patch that is larger than the preimage'

. ./test-lib.sh

cat >F  <<\EOF
1
2
3
4
5
6
7
8
999999
A
B
C
D
E
F
G
H
I
J

EOF

test_expect_success setup '

	but add F &&
	mv F G &&
	sed -e "s/1/11/" -e "s/999999/9/" -e "s/H/HH/" <G >F &&
	but diff >patch &&
	sed -e "/^\$/d" <G >F &&
	but add F

'

test_expect_success 'apply should fail gracefully' '
	test_must_fail but apply --index patch &&
	test_path_is_missing .but/index.lock
'

test_done
