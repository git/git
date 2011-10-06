#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='our own option parser'

. ./test-lib.sh

cat > expect << EOF
usage: test-parse-options <options>

    -b, --boolean         get a boolean
    -4, --or4             bitwise-or boolean with ...0100
    --neg-or4             same as --no-or4

    -i, --integer <n>     get a integer
    -j <n>                get a integer, too
    --set23               set integer to 23
    -t <time>             get timestamp of <time>
    -L, --length <str>    get length of <str>
    -F, --file <file>     set file to <file>

String options
    -s, --string <string>
                          get a string
    --string2 <str>       get another string
    --st <st>             get another string (pervert ordering)
    -o <str>              get another string
    --default-string      set string to default
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

EOF

test_expect_success 'test help' '
	test_must_fail test-parse-options -h > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

mv expect expect.err

cat > expect << EOF
boolean: 2
integer: 1729
timestamp: 0
string: 123
abbrev: 7
verbose: 2
quiet: no
dry run: yes
file: prefix/my.file
EOF

test_expect_success 'short options' '
	test-parse-options -s123 -b -i 1729 -b -vv -n -F my.file \
	> output 2> output.err &&
	test_cmp expect output &&
	test ! -s output.err
'

cat > expect << EOF
boolean: 2
integer: 1729
timestamp: 0
string: 321
abbrev: 10
verbose: 2
quiet: no
dry run: no
file: prefix/fi.le
EOF

test_expect_success 'long options' '
	test-parse-options --boolean --integer 1729 --boolean --string2=321 \
		--verbose --verbose --no-dry-run --abbrev=10 --file fi.le\
		--obsolete > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'missing required value' '
	test-parse-options -s;
	test $? = 129 &&
	test-parse-options --string;
	test $? = 129 &&
	test-parse-options --file;
	test $? = 129
'

cat > expect << EOF
boolean: 1
integer: 13
timestamp: 0
string: 123
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
arg 00: a1
arg 01: b1
arg 02: --boolean
EOF

test_expect_success 'intermingled arguments' '
	test-parse-options a1 --string 123 b1 --boolean -j 13 -- --boolean \
		> output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > expect << EOF
boolean: 0
integer: 2
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'unambiguously abbreviated option' '
	test-parse-options --int 2 --boolean --no-bo > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'unambiguously abbreviated option with "="' '
	test-parse-options --int=2 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'ambiguously abbreviated option' '
	test-parse-options --strin 123;
	test $? = 129
'

cat > expect << EOF
boolean: 0
integer: 0
timestamp: 0
string: 123
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'non ambiguous option (after two options it abbreviates)' '
	test-parse-options --st 123 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > typo.err << EOF
error: did you mean \`--boolean\` (with two dashes ?)
EOF

test_expect_success 'detect possible typos' '
	test_must_fail test-parse-options -boolean > output 2> output.err &&
	test ! -s output &&
	test_cmp typo.err output.err
'

cat > expect <<EOF
boolean: 0
integer: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
arg 00: --quux
EOF

test_expect_success 'keep some options as arguments' '
	test-parse-options --quux > output 2> output.err &&
        test ! -s output.err &&
        test_cmp expect output
'

cat > expect <<EOF
boolean: 0
integer: 0
timestamp: 1
string: default
abbrev: 7
verbose: 0
quiet: yes
dry run: no
file: (not set)
arg 00: foo
EOF

test_expect_success 'OPT_DATE() and OPT_SET_PTR() work' '
	test-parse-options -t "1970-01-01 00:00:01 +0000" --default-string \
		foo -q > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > expect <<EOF
Callback: "four", 0
boolean: 5
integer: 4
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_CALLBACK() and OPT_BIT() work' '
	test-parse-options --length=four -b -4 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > expect <<EOF
Callback: "not set", 1
EOF

test_expect_success 'OPT_CALLBACK() and callback errors work' '
	test_must_fail test-parse-options --no-length > output 2> output.err &&
	test_cmp expect output &&
	test_cmp expect.err output.err
'

cat > expect <<EOF
boolean: 1
integer: 23
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_BIT() and OPT_SET_INT() work' '
	test-parse-options --set23 -bbbbb --no-or4 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_NEGBIT() and OPT_SET_INT() work' '
	test-parse-options --set23 -bbbbb --neg-or4 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > expect <<EOF
boolean: 6
integer: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_BIT() works' '
	test-parse-options -bb --or4 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_NEGBIT() works' '
	test-parse-options -bb --no-neg-or4 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

test_expect_success 'OPT_BOOLEAN() with PARSE_OPT_NODASH works' '
	test-parse-options + + + + + + > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat > expect <<EOF
boolean: 0
integer: 12345
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'OPT_NUMBER_CALLBACK() works' '
	test-parse-options -12345 > output 2> output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat >expect <<EOF
boolean: 0
integer: 0
timestamp: 0
string: (not set)
abbrev: 7
verbose: 0
quiet: no
dry run: no
file: (not set)
EOF

test_expect_success 'negation of OPT_NONEG flags is not ambiguous' '
	test-parse-options --no-ambig >output 2>output.err &&
	test ! -s output.err &&
	test_cmp expect output
'

cat >>expect <<'EOF'
list: foo
list: bar
list: baz
EOF
test_expect_success '--list keeps list of strings' '
	test-parse-options --list foo --list=bar --list=baz >output &&
	test_cmp expect output
'

test_expect_success '--no-list resets list' '
	test-parse-options --list=other --list=irrelevant --list=options \
		--no-list --list=foo --list=bar --list=baz >output &&
	test_cmp expect output
'

test_done
