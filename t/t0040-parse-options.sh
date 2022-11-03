#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='our own option parser'

TEST_PASSES_SANITIZE_LEAK=true
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
    --mode1               set integer to 1 (cmdmode option)
    --mode2               set integer to 2 (cmdmode option)
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
    -NUM                  set integer to NUM
    +                     same as -b
    --ambiguous           positive ambiguity
    --no-ambiguous        negative ambiguity

Standard options
    --abbrev[=<n>]        use <n> digits to display object names
    -v, --verbose         be verbose
    -n, --dry-run         dry run
    -q, --quiet           be quiet
    --expect <string>     expected output in the variable dump

Alias
    -A, --alias-source <string>
                          get a string
    -Z, --alias-target <string>
                          alias of --alias-source

EOF

test_expect_success 'test help' '
	test_must_fail test-tool parse-options -h >output 2>output.err &&
	test_must_be_empty output.err &&
	test_cmp expect output
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
	test_cmp expect output.err
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
	cat >expect <<-\EOF &&
	error: switch `s'\'' requires a value
	EOF
	test_expect_code 129 test-tool parse-options -s 2>actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	error: option `string'\'' requires a value
	EOF
	test_expect_code 129 test-tool parse-options --string 2>actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	error: option `file'\'' requires a value
	EOF
	test_expect_code 129 test-tool parse-options --file 2>actual &&
	test_cmp expect actual
'

test_expect_success 'superfluous value provided: boolean' '
	cat >expect <<-\EOF &&
	error: option `yes'\'' takes no value
	EOF
	test_expect_code 129 test-tool parse-options --yes=hi 2>actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	error: option `no-yes'\'' takes no value
	EOF
	test_expect_code 129 test-tool parse-options --no-yes=hi 2>actual &&
	test_cmp expect actual
'

test_expect_success 'superfluous value provided: cmdmode' '
	cat >expect <<-\EOF &&
	error: option `mode1'\'' takes no value
	EOF
	test_expect_code 129 test-tool parse-options --mode1=hi 2>actual &&
	test_cmp expect actual
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
error: did you mean `--boolean` (with two dashes)?
EOF

test_expect_success 'detect possible typos' '
	test_must_fail test-tool parse-options -boolean >output 2>output.err &&
	test_must_be_empty output &&
	test_cmp typo.err output.err
'

cat >typo.err <<\EOF
error: did you mean `--ambiguous` (with two dashes)?
EOF

test_expect_success 'detect possible typos' '
	test_must_fail test-tool parse-options -ambiguous >output 2>output.err &&
	test_must_be_empty output &&
	test_cmp typo.err output.err
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

test_expect_success 'OPT_CMDMODE() works' '
	test-tool parse-options --expect="integer: 1" --mode1
'

test_expect_success 'OPT_CMDMODE() detects incompatibility' '
	test_must_fail test-tool parse-options --mode1 --mode2 >output 2>output.err &&
	test_must_be_empty output &&
	test_i18ngrep "incompatible with --mode" output.err
'

test_expect_success 'OPT_CMDMODE() detects incompatibility with something else' '
	test_must_fail test-tool parse-options --set23 --mode2 >output 2>output.err &&
	test_must_be_empty output &&
	test_i18ngrep "incompatible with something else" output.err
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

test_expect_success 'KEEP_DASHDASH works' '
	test-tool parse-options-flags --keep-dashdash cmd --opt=1 -- --opt=2 --unknown >actual &&
	cat >expect <<-\EOF &&
	opt: 1
	arg 00: --
	arg 01: --opt=2
	arg 02: --unknown
	EOF
	test_cmp expect actual
'

test_expect_success 'KEEP_ARGV0 works' '
	test-tool parse-options-flags --keep-argv0 cmd arg0 --opt=3 >actual &&
	cat >expect <<-\EOF &&
	opt: 3
	arg 00: cmd
	arg 01: arg0
	EOF
	test_cmp expect actual
'

test_expect_success 'STOP_AT_NON_OPTION works' '
	test-tool parse-options-flags --stop-at-non-option cmd --opt=4 arg0 --opt=5 --unknown >actual &&
	cat >expect <<-\EOF &&
	opt: 4
	arg 00: arg0
	arg 01: --opt=5
	arg 02: --unknown
	EOF
	test_cmp expect actual
'

test_expect_success 'KEEP_UNKNOWN_OPT works' '
	test-tool parse-options-flags --keep-unknown-opt cmd --unknown=1 --opt=6 -u2 >actual &&
	cat >expect <<-\EOF &&
	opt: 6
	arg 00: --unknown=1
	arg 01: -u2
	EOF
	test_cmp expect actual
'

test_expect_success 'NO_INTERNAL_HELP works for -h' '
	test_expect_code 129 test-tool parse-options-flags --no-internal-help cmd -h 2>err &&
	grep "^error: unknown switch \`h$SQ" err &&
	grep "^usage: " err
'

for help_opt in help help-all
do
	test_expect_success "NO_INTERNAL_HELP works for --$help_opt" "
		test_expect_code 129 test-tool parse-options-flags --no-internal-help cmd --$help_opt 2>err &&
		grep '^error: unknown option \`'$help_opt\' err &&
		grep '^usage: ' err
	"
done

test_expect_success 'KEEP_UNKNOWN_OPT | NO_INTERNAL_HELP works' '
	test-tool parse-options-flags --keep-unknown-opt --no-internal-help cmd -h --help --help-all >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	arg 00: -h
	arg 01: --help
	arg 02: --help-all
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - no subcommand shows error and usage' '
	test_expect_code 129 test-tool parse-subcommand cmd 2>err &&
	grep "^error: need a subcommand" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - subcommand after -- shows error and usage' '
	test_expect_code 129 test-tool parse-subcommand cmd -- subcmd-one 2>err &&
	grep "^error: need a subcommand" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - subcommand after --end-of-options shows error and usage' '
	test_expect_code 129 test-tool parse-subcommand cmd --end-of-options subcmd-one 2>err &&
	grep "^error: need a subcommand" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - unknown subcommand shows error and usage' '
	test_expect_code 129 test-tool parse-subcommand cmd nope 2>err &&
	grep "^error: unknown subcommand: \`nope$SQ" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - subcommands cannot be abbreviated' '
	test_expect_code 129 test-tool parse-subcommand cmd subcmd-o 2>err &&
	grep "^error: unknown subcommand: \`subcmd-o$SQ$" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - no negated subcommands' '
	test_expect_code 129 test-tool parse-subcommand cmd no-subcmd-one 2>err &&
	grep "^error: unknown subcommand: \`no-subcmd-one$SQ" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - simple' '
	test-tool parse-subcommand cmd subcmd-two >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_two
	arg 00: subcmd-two
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - stop parsing at the first subcommand' '
	test-tool parse-subcommand cmd --opt=1 subcmd-two subcmd-one --opt=2 >actual &&
	cat >expect <<-\EOF &&
	opt: 1
	fn: subcmd_two
	arg 00: subcmd-two
	arg 01: subcmd-one
	arg 02: --opt=2
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - KEEP_ARGV0' '
	test-tool parse-subcommand --keep-argv0 cmd subcmd-two >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_two
	arg 00: cmd
	arg 01: subcmd-two
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL + subcommand not given' '
	test-tool parse-subcommand --subcommand-optional cmd >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL + given subcommand' '
	test-tool parse-subcommand --subcommand-optional cmd subcmd-two branch file >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_two
	arg 00: subcmd-two
	arg 01: branch
	arg 02: file
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL + subcommand not given + unknown dashless args' '
	test-tool parse-subcommand --subcommand-optional cmd branch file >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: branch
	arg 01: file
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL + subcommand not given + unknown option' '
	test_expect_code 129 test-tool parse-subcommand --subcommand-optional cmd --subcommand-opt 2>err &&
	grep "^error: unknown option" err &&
	grep ^usage: err
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL | KEEP_UNKNOWN_OPT + subcommand not given + unknown option' '
	test-tool parse-subcommand --subcommand-optional --keep-unknown-opt cmd --subcommand-opt >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: --subcommand-opt
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL | KEEP_UNKNOWN_OPT + subcommand ignored after unknown option' '
	test-tool parse-subcommand --subcommand-optional --keep-unknown-opt cmd --subcommand-opt subcmd-two >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: --subcommand-opt
	arg 01: subcmd-two
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL | KEEP_UNKNOWN_OPT + command and subcommand options cannot be mixed' '
	test-tool parse-subcommand --subcommand-optional --keep-unknown-opt cmd --subcommand-opt branch --opt=1 >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: --subcommand-opt
	arg 01: branch
	arg 02: --opt=1
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL | KEEP_UNKNOWN_OPT | KEEP_ARGV0' '
	test-tool parse-subcommand --subcommand-optional --keep-unknown-opt --keep-argv0 cmd --subcommand-opt branch >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: cmd
	arg 01: --subcommand-opt
	arg 02: branch
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - SUBCOMMAND_OPTIONAL | KEEP_UNKNOWN_OPT | KEEP_DASHDASH' '
	test-tool parse-subcommand --subcommand-optional --keep-unknown-opt --keep-dashdash cmd -- --subcommand-opt file >actual &&
	cat >expect <<-\EOF &&
	opt: 0
	fn: subcmd_one
	arg 00: --
	arg 01: --subcommand-opt
	arg 02: file
	EOF
	test_cmp expect actual
'

test_expect_success 'subcommand - completion helper' '
	test-tool parse-subcommand cmd --git-completion-helper >actual &&
	echo "subcmd-one subcmd-two --opt= --no-opt" >expect &&
	test_cmp expect actual
'

test_expect_success 'subcommands are incompatible with STOP_AT_NON_OPTION' '
	test_must_fail test-tool parse-subcommand --stop-at-non-option cmd subcmd-one 2>err &&
	grep ^BUG err
'

test_expect_success 'subcommands are incompatible with KEEP_UNKNOWN_OPT unless in combination with SUBCOMMAND_OPTIONAL' '
	test_must_fail test-tool parse-subcommand --keep-unknown-opt cmd subcmd-two 2>err &&
	grep ^BUG err
'

test_expect_success 'subcommands are incompatible with KEEP_DASHDASH unless in combination with SUBCOMMAND_OPTIONAL' '
	test_must_fail test-tool parse-subcommand --keep-dashdash cmd subcmd-two 2>err &&
	grep ^BUG err
'

test_done
