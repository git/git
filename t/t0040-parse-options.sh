#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='our own option parser'

. ./test-lib.sh

cat >expect <<\EOF
usage: test-tool parse-options <options>

    A helper function for the parse-options API.

    --yes                 get a boolean
    -D, --no-doubt        begins with 'no-'
    -B, --no-fear         be brave
    -b, --boolean         increment by one
    -4, --or4             bitwise-or boolean with ...0100
    --neg-or4             same as --no-or4

    -i, --integer <n>     get a integer
    -j <n>                get a integer, too
    -m, --magnitude <n>   get a magnitude
    --set23               set integer to 23
    -L, --length <str>    get length of <str>
    -F, --file <file>     set file to <file>

String options
    -s, --string <string>
                          get a string
    --string2 <str>       get another string
    --st <st>             get another string (pervert ordering)
    -o <str>              get another string
    --list <str>          add str to list

Magic arguments
    --quux                means --quux
    -NUM                  set integer to NUM
    +                     same as -b
    --ambiguous           positive ambiguity
    --no-ambiguous        negative ambiguity

Standard options
    --abbrev[=<n>]        use <n> digits to display SHA-1s
    -v, --verbose         be verbose
    -n, --dry-run         dry run
    -q, --quiet           be quiet
    --expect <string>     expected output in the variable dump

Alias
    -A, --alias-source <string>
                          get a string
    -Z, --alias-target <string>
                          get a string

EOF

test_expect_success 'test help' '
	test_must_fail test-tool parse-options -h >output 2>output.err &&
	test_must_be_empty output.err &&
	test_i18ncmp expect output
'

mv expect expect.err

check () {
	what="$1" &&
	shift &&
	expect="$1" &&
	shift &&
	test-tool parse-options --expect="$what $expect" "$@"
}

check_unknown_i18n() {
	case "$1" in
	--*)
		echo error: unknown option \`${1#--}\' >expect ;;
	-*)
		echo error: unknown switch \`${1#-}\' >expect ;;
	esac &&
	cat expect.err >>expect &&
	test_must_fail test-tool parse-options $* >output 2>output.err &&
	test_must_be_empty output &&
	test_i18ncmp expect output.err
}

test_expect_success 'OPT_BOOL() #1' 'check boolean: 1 --yes'
test_expect_success 'OPT_BOOL() #2' 'check boolean: 1 --no-doubt'
test_expect_success 'OPT_BOOL() #3' 'check boolean: 1 -D'
test_expect_success 'OPT_BOOL() #4' 'check boolean: 1 --no-fear'
test_expect_success 'OPT_BOOL() #5' 'check boolean: 1 -B'

test_expect_success 'OPT_BOOL() is idempotent #1' 'check boolean: 1 --yes --yes'
test_expect_success 'OPT_BOOL() is idempotent #2' 'check boolean: 1 -DB'

test_expect_success 'OPT_BOOL() negation #1' 'check boolean: 0 -D --no-yes'
test_expect_success 'OPT_BOOL() negation #2' 'check boolean: 0 -D --no-no-doubt'

test_expect_success 'OPT_BOOL() no negation #1' 'check_unknown_i18n --fear'
test_expect_success 'OPT_BOOL() no negation #2' 'check_unknown_i18n --no-no-fear'

test_expect_success 'OPT_BOOL() positivation' 'check boolean: 0 -D --doubt'

test_expect_success 'OPT_INT() negative' 'check integer: -2345 -i -2345'

test_expect_success 'OPT_MAGNITUDE() simple' '
	check magnitude: 2345678 -m 2345678
'

test_expect_success 'OPT_MAGNITUDE() kilo' '
	check magnitude: 239616 -m 234k
'

test_expect_success 'OPT_MAGNITUDE() mega' '
	check magnitude: 104857600 -m 100m
'

test_expect_success 'OPT_MAGNITUDE() giga' '
	check magnitude: 1073741824 -m 1g
'

test_expect_success 'OPT_MAGNITUDE() 3giga' '
	check magnitude: 3221225472 -m 3g
'

cat >expect <<\EOF
boolean: 2
integer: 1729
magnitude: 16384
timestamp: 0
string: 123
abbrev: 7
verbose: 2
quiet: 0
dry run: yes
file: prefix/my.file
EOF

test_expect_success 'short options' '
	test-tool parse-options -s123 -b -i 1729 -m 16k -b -vv -n -F my.file \
	>output 2>output.err &&
	test_cmp expect output &&
	test_must_be_empty output.err
'

cat >expect <<\EOF
boolean: 2
integer: 1729
magnitude: 16384
timestamp: 0
string: 321
abbrev: 10
verbose: 2
quiet: 0
dry run: no
file: prefix/fi.le
EOF

test_expect_success 'long options' '
	test-tool parse-options --boolean --integer 1729 --magnitude 16k \
		--boolean --string2=321 --verbose --verbose --no-dry-run \
		--abbrev=10 --file fi.le --obsolete \
		>output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

test_expect_success 'missing required value' '
	test_expect_code 129 test-tool parse-options -s &&
	test_expect_code 129 test-tool parse-options --string &&
	test_expect_code 129 test-tool parse-options --file
'

cat >expect <<\EOF
boolean: 1
integer: 13
magnitude: 0
timestamp: 0
string: 123
abbrev: 7
verbose: -1
quiet: 0
dry run: no
file: (not set)
arg 00: a1
arg 01: b1
arg 02: --boolean
EOF

test_expect_success 'intermingled arguments' '
	test-tool parse-options a1 --string 123 b1 --boolean -j 13 -- --boolean \
		>output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

cat >expect <<\EOF
boolean: 0
integer: 2
magnitude: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: -1
quiet: 0
dry run: no
file: (not set)
EOF

test_expect_success 'unambiguously abbreviated option' '
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --int 2 --boolean --no-bo >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

test_expect_success 'unambiguously abbreviated option with "="' '
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --expect="integer: 2" --int=2
'

test_expect_success 'ambiguously abbreviated option' '
	test_expect_code 129 env GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --strin 123
'

test_expect_success 'non ambiguous option (after two options it abbreviates)' '
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --expect="string: 123" --st 123
'

test_expect_success 'Alias options do not contribute to abbreviation' '
	test-tool parse-options --alias-source 123 >output &&
	grep "^string: 123" output &&
	test-tool parse-options --alias-target 123 >output &&
	grep "^string: 123" output &&
	test_must_fail test-tool parse-options --alias &&
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --alias 123 >output &&
	grep "^string: 123" output
'

cat >typo.err <<\EOF
error: did you mean `--boolean` (with two dashes ?)
EOF

test_expect_success 'detect possible typos' '
	test_must_fail test-tool parse-options -boolean >output 2>output.err &&
	test_must_be_empty output &&
	test_i18ncmp typo.err output.err
'

cat >typo.err <<\EOF
error: did you mean `--ambiguous` (with two dashes ?)
EOF

test_expect_success 'detect possible typos' '
	test_must_fail test-tool parse-options -ambiguous >output 2>output.err &&
	test_must_be_empty output &&
	test_i18ncmp typo.err output.err
'

test_expect_success 'keep some options as arguments' '
	test-tool parse-options --expect="arg 00: --quux" --quux
'

cat >expect <<\EOF
Callback: "four", 0
boolean: 5
integer: 4
magnitude: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: -1
quiet: 0
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_CALLBACK() and OPT_BIT() work' '
	test-tool parse-options --length=four -b -4 >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_CALLBACK() and callback errors work' '
	test_must_fail test-tool parse-options --no-length >output 2>output.err &&
	test_must_be_empty output &&
	test_must_be_empty output.err
'

cat >expect <<\EOF
boolean: 1
integer: 23
magnitude: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: -1
quiet: 0
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_BIT() and OPT_SET_INT() work' '
	test-tool parse-options --set23 -bbbbb --no-or4 >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_NEGBIT() and OPT_SET_INT() work' '
	test-tool parse-options --set23 -bbbbb --neg-or4 >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_BIT() works' '
	test-tool parse-options --expect="boolean: 6" -bb --or4
'

test_expect_success 'OPT_NEGBIT() works' '
	test-tool parse-options --expect="boolean: 6" -bb --no-neg-or4
'

test_expect_success 'OPT_COUNTUP() with PARSE_OPT_NODASH works' '
	test-tool parse-options --expect="boolean: 6" + + + + + +
'

test_expect_success 'OPT_NUMBER_CALLBACK() works' '
	test-tool parse-options --expect="integer: 12345" -12345
'

cat >expect <<\EOF
boolean: 0
integer: 0
magnitude: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: -1
quiet: 0
dry run: no
file: (not set)
EOF

test_expect_success 'negation of OPT_NONEG flags is not ambiguous' '
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	test-tool parse-options --no-ambig >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
'

cat >>expect <<\EOF
list: foo
list: bar
list: baz
EOF
test_expect_success '--list keeps list of strings' '
	test-tool parse-options --list foo --list=bar --list=baz >output &&
	test_cmp expect output
'

test_expect_success '--no-list resets list' '
	test-tool parse-options --list=other --list=irrelevant --list=options \
		--no-list --list=foo --list=bar --list=baz >output &&
	test_cmp expect output
'

test_expect_success 'multiple quiet levels' '
	test-tool parse-options --expect="quiet: 3" -q -q -q
'

test_expect_success 'multiple verbose levels' '
	test-tool parse-options --expect="verbose: 3" -v -v -v
'

test_expect_success '--no-quiet sets --quiet to 0' '
	test-tool parse-options --expect="quiet: 0" --no-quiet
'

test_expect_success '--no-quiet resets multiple -q to 0' '
	test-tool parse-options --expect="quiet: 0" -q -q -q --no-quiet
'

test_expect_success '--no-verbose sets verbose to 0' '
	test-tool parse-options --expect="verbose: 0" --no-verbose
'

test_expect_success '--no-verbose resets multiple verbose to 0' '
	test-tool parse-options --expect="verbose: 0" -v -v -v --no-verbose
'

test_expect_success 'GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS works' '
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
		test-tool parse-options --ye &&
	test_must_fail env GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=true \
		test-tool parse-options --ye
'

test_expect_success '--end-of-options treats remainder as args' '
	test-tool parse-options \
	    --expect="verbose: -1" \
	    --expect="arg 00: --verbose" \
	    --end-of-options --verbose
'

test_done
