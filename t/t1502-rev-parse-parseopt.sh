#!/bin/sh

test_description='test git rev-parse --parseopt'

. ./test-lib.sh

check_invalid_long_option () {
	spec="$1"
	opt="$2"
	test_expect_success "test --parseopt invalid switch $opt help output for $spec" '
		{
			cat <<-\EOF &&
			error: unknown option `'${opt#--}\''
			EOF
			sed -e 1d -e \$d <"$TEST_DIRECTORY/t1502/$spec.help"
		} >expect &&
		test_expect_code 129 git rev-parse --parseopt -- $opt \
			2>output <"$TEST_DIRECTORY/t1502/$spec" &&
		test_cmp expect output
	'
}

test_expect_success 'setup optionspec' '
	sed -e "s/^|//" >optionspec <<\EOF
|some-command [options] <args>...
|
|some-command does foo and bar!
|--
|h,help!   show the help
|
|foo       some nifty option --foo
|bar=      some cool option --bar with an argument
|b,baz     a short and long option
|
| An option group Header
|C?        option C with an optional argument
|d,data?   short and long option with an optional argument
|
| Argument hints
|B=arg     short option required argument
|bar2=arg  long option required argument
|e,fuz=with-space  short and long option required argument
|s?some    short option optional argument
|long?data long option optional argument
|g,fluf?path     short and long option optional argument
|longest=very-long-argument-hint  a very long argument hint
|pair=key=value  with an equals sign in the hint
|aswitch help te=t contains? fl*g characters!`
|bswitch=hint	 hint has trailing tab character
|cswitch	 switch has trailing tab character
|short-hint=a    with a one symbol hint
|
|Extras
|extra1    line above used to cause a segfault but no longer does
EOF
'

test_expect_success 'setup optionspec-no-switches' '
	sed -e "s/^|//" >optionspec_no_switches <<\EOF
|some-command [options] <args>...
|
|some-command does foo and bar!
|--
EOF
'

test_expect_success 'setup optionspec-only-hidden-switches' '
	sed -e "s/^|//" >optionspec_only_hidden_switches <<\EOF
|some-command [options] <args>...
|
|some-command does foo and bar!
|--
|hidden1* A hidden switch
EOF
'

test_expect_success 'test --parseopt help output' '
	test_expect_code 129 git rev-parse --parseopt -- -h > output < optionspec &&
	test_cmp "$TEST_DIRECTORY/t1502/optionspec.help" output
'

test_expect_success 'test --parseopt help output no switches' '
	sed -e "s/^|//" >expect <<\END_EXPECT &&
|cat <<\EOF
|usage: some-command [options] <args>...
|
|    some-command does foo and bar!
|
|EOF
END_EXPECT
	test_expect_code 129 git rev-parse --parseopt -- -h > output < optionspec_no_switches &&
	test_cmp expect output
'

test_expect_success 'test --parseopt help output hidden switches' '
	sed -e "s/^|//" >expect <<\END_EXPECT &&
|cat <<\EOF
|usage: some-command [options] <args>...
|
|    some-command does foo and bar!
|
|EOF
END_EXPECT
	test_expect_code 129 git rev-parse --parseopt -- -h > output < optionspec_only_hidden_switches &&
	test_cmp expect output
'

test_expect_success 'test --parseopt help-all output hidden switches' '
	sed -e "s/^|//" >expect <<\END_EXPECT &&
|cat <<\EOF
|usage: some-command [options] <args>...
|
|    some-command does foo and bar!
|
|    --[no-]hidden1        A hidden switch
|
|EOF
END_EXPECT
	test_expect_code 129 git rev-parse --parseopt -- --help-all > output < optionspec_only_hidden_switches &&
	test_cmp expect output
'

test_expect_success 'test --parseopt invalid switch help output' '
	{
		cat <<-\EOF &&
		error: unknown option `does-not-exist'\''
		EOF
		sed -e 1d -e \$d <"$TEST_DIRECTORY/t1502/optionspec.help"
	} >expect &&
	test_expect_code 129 git rev-parse --parseopt -- --does-not-exist 1>/dev/null 2>output < optionspec &&
	test_cmp expect output
'

test_expect_success 'setup expect.1' "
	cat > expect <<EOF
set -- --foo --bar 'ham' -b --aswitch -- 'arg'
EOF
"

test_expect_success 'test --parseopt' '
	git rev-parse --parseopt -- --foo --bar=ham --baz --aswitch arg < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt with mixed options and arguments' '
	git rev-parse --parseopt -- --foo arg --bar=ham --baz --aswitch < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.2' "
	cat > expect <<EOF
set -- --foo -- 'arg' '--bar=ham'
EOF
"

test_expect_success 'test --parseopt with --' '
	git rev-parse --parseopt -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt --stop-at-non-option' '
	git rev-parse --parseopt --stop-at-non-option -- --foo arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.3' "
	cat > expect <<EOF
set -- --foo -- '--' 'arg' '--bar=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash' '
	git rev-parse --parseopt --keep-dashdash -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.4' "
	cat >expect <<EOF
set -- --foo -- '--' 'arg' '--spam=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option with --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo -- arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.5' "
	cat > expect <<EOF
set -- --foo -- 'arg' '--spam=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option without --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.6' "
	cat > expect <<EOF
set -- --foo --bar='z' --baz -C'Z' --data='A' -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long' '
	git rev-parse --parseopt --stuck-long -- --foo --bar=z -b arg -CZ -dA <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.7' "
	cat > expect <<EOF
set -- --data='' -C --baz -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long and empty optional argument' '
	git rev-parse --parseopt --stuck-long -- --data= arg -C -b <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.8' "
	cat > expect <<EOF
set -- --data --baz -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long and long option with unset optional argument' '
	git rev-parse --parseopt --stuck-long -- --data arg -b <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt --stuck-long and short option with unset optional argument' '
	git rev-parse --parseopt --stuck-long -- -d arg -b <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt help output: "wrapped" options normal "or:" lines' '
	sed -e "s/^|//" >spec <<-\EOF &&
	|cmd [--some-option]
	|    [--another-option]
	|cmd [--yet-another-option]
	|--
	|h,help!   show the help
	EOF

	sed -e "s/^|//" >expect <<-\END_EXPECT &&
	|cat <<\EOF
	|usage: cmd [--some-option]
	|   or:     [--another-option]
	|   or: cmd [--yet-another-option]
	|
	|    -h, --help            show the help
	|
	|EOF
	END_EXPECT

	test_must_fail git rev-parse --parseopt -- -h <spec >actual &&
	test_cmp expect actual
'

test_expect_success 'test --parseopt invalid opt-spec' '
	test_write_lines x -- "=, x" >spec &&
	echo "fatal: missing opt-spec before option flags" >expect &&
	test_must_fail git rev-parse --parseopt -- <spec 2>err &&
	test_cmp expect err
'

test_expect_success 'test --parseopt help output: multi-line blurb after empty line' '
	sed -e "s/^|//" >spec <<-\EOF &&
	|cmd [--some-option]
	|    [--another-option]
	|
	|multi
	|line
	|blurb
	|--
	|h,help!   show the help
	EOF

	sed -e "s/^|//" >expect <<-\END_EXPECT &&
	|cat <<\EOF
	|usage: cmd [--some-option]
	|   or:     [--another-option]
	|
	|    multi
	|    line
	|    blurb
	|
	|    -h, --help            show the help
	|
	|EOF
	END_EXPECT

	test_must_fail git rev-parse --parseopt -- -h <spec >actual &&
	test_cmp expect actual
'

test_expect_success 'test --parseopt help output for optionspec-neg' '
	test_expect_code 129 git rev-parse --parseopt -- \
		-h >output <"$TEST_DIRECTORY/t1502/optionspec-neg" &&
	test_cmp "$TEST_DIRECTORY/t1502/optionspec-neg.help" output
'

test_expect_success 'test --parseopt valid options for optionspec-neg' '
	cat >expect <<-\EOF &&
	set -- --foo --no-foo --no-bar --positive-only --no-negative --
	EOF
	git rev-parse --parseopt -- <"$TEST_DIRECTORY/t1502/optionspec-neg" >output \
	       --foo --no-foo --no-bar --positive-only --no-negative &&
	test_cmp expect output
'

test_expect_success 'test --parseopt positivated option for optionspec-neg' '
	cat >expect <<-\EOF &&
	set -- --no-no-bar --no-no-bar --
	EOF
	git rev-parse --parseopt -- <"$TEST_DIRECTORY/t1502/optionspec-neg" >output \
	       --no-no-bar --bar &&
	test_cmp expect output
'

check_invalid_long_option optionspec-neg --no-positive-only
check_invalid_long_option optionspec-neg --negative
check_invalid_long_option optionspec-neg --no-no-negative

test_expect_success 'ambiguous: --no matches both --noble and --no-noble' '
	cat >spec <<-\EOF &&
	some-command [options]
	--
	noble The feudal switch.
	EOF
	test_expect_code 129 env GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
	git rev-parse --parseopt -- <spec 2>err --no &&
	grep "error: ambiguous option: no (could be --noble or --no-noble)" err
'

test_done
