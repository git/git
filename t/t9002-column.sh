#!/bin/sh

test_description='git column'
. ./test-lib.sh

test_expect_success 'setup' '
	cat >lista <<\EOF
one
two
three
four
five
six
seven
eight
nine
ten
eleven
EOF
'

test_expect_success 'never' '
	git column --indent=Z --mode=never <lista >actual &&
	test_cmp lista actual
'

test_expect_success 'always' '
	cat >expected <<\EOF &&
Zone
Ztwo
Zthree
Zfour
Zfive
Zsix
Zseven
Zeight
Znine
Zten
Zeleven
EOF
	git column --indent=Z --mode=plain <lista >actual &&
	test_cmp expected actual
'

test_done
