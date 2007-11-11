#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='our own option parser'

. ./test-lib.sh

cat > expect.err << EOF
usage: test-parse-options <options>

    -b, --boolean         get a boolean
    -i, --integer <n>     get a integer
    -j <n>                get a integer, too

string options
    -s, --string <string>
                          get a string
    --string2 <str>       get another string
    --st <st>             get another string (pervert ordering)

EOF

test_expect_success 'test help' '
	! test-parse-options -h > output 2> output.err &&
	test ! -s output &&
	git diff expect.err output.err
'

cat > expect << EOF
boolean: 2
integer: 1729
string: 123
EOF

test_expect_success 'short options' '
	test-parse-options -s123 -b -i 1729 -b > output 2> output.err &&
	git diff expect output &&
	test ! -s output.err
'
cat > expect << EOF
boolean: 2
integer: 1729
string: 321
EOF

test_expect_success 'long options' '
	test-parse-options --boolean --integer 1729 --boolean --string2=321 \
		> output 2> output.err &&
	test ! -s output.err &&
	git diff expect output
'

cat > expect << EOF
boolean: 1
integer: 13
string: 123
arg 00: a1
arg 01: b1
arg 02: --boolean
EOF

test_expect_success 'intermingled arguments' '
	test-parse-options a1 --string 123 b1 --boolean -j 13 -- --boolean \
		> output 2> output.err &&
	test ! -s output.err &&
	git diff expect output
'

cat > expect << EOF
boolean: 0
integer: 2
string: (not set)
EOF

test_expect_success 'unambiguously abbreviated option' '
	test-parse-options --int 2 --boolean --no-bo > output 2> output.err &&
	test ! -s output.err &&
	git diff expect output
'

test_expect_success 'unambiguously abbreviated option with "="' '
	test-parse-options --int=2 > output 2> output.err &&
	test ! -s output.err &&
	git diff expect output
'

test_expect_failure 'ambiguously abbreviated option' '
	test-parse-options --strin 123;
        test $? != 129
'

cat > expect << EOF
boolean: 0
integer: 0
string: 123
EOF

test_expect_success 'non ambiguous option (after two options it abbreviates)' '
	test-parse-options --st 123 > output 2> output.err &&
	test ! -s output.err &&
	git diff expect output
'

test_done
